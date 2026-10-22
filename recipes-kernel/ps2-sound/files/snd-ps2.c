// SPDX-License-Identifier: GPL-2.0
/*
 * ALSA driver for the PlayStation 2 sound processor (SPU2).
 *
 * The SPU2 hangs off the IOP, so the IOP module spu2 drives it. This
 * driver gives ALSA one playback device: 48 kHz, 16-bit, stereo. It
 * copies the ALSA buffer to a ring in IOP memory in chunks, over the SIF,
 * and the IOP module reports what it played. The protocol is in
 * spu2-sif.h.
 *
 * A tasklet sends the chunks and the start and stop commands. The ack
 * callback (an application wrote samples), the SIF callback (the IOP
 * reports a played chunk) and the trigger only schedule it. They run
 * with interrupts off, and a SIF send must not wait with interrupts off:
 * the IOP may answer with a packet that needs the SIF0 interrupt here.
 *
 * A program that wants another rate or format uses the ALSA "plug"
 * layer, which the default device has.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/completion.h>
#include <linux/interrupt.h>
#include <linux/ktime.h>
#include <linux/math64.h>

#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/control.h>
#include <sound/initval.h>

#include <asm/mach-ps2/iop-module.h>
#include <asm/mach-ps2/sif.h>

#include "spu2-sif.h"

/* The kernel exports this loader of ROM modules, but declares it nowhere. */
int iop_module_load_rom(const char *filepath);

#define DRV_NAME "snd-ps2"

/*
 * xrun=1: report the position the standard way, so ALSA declares an
 * underrun when the program stalls and the program restarts the stream.
 * The default holds the last chunk back: the hardware plays silence for
 * the length of the stall and the stream goes on.
 */
static bool xrun;
module_param(xrun, bool, 0644);
MODULE_PARM_DESC(xrun, "declare ALSA underruns when the program stalls (default: no)");

/*
 * debug=1: the first events of a stream in the kernel log (the start, the
 * first report from the IOP), the cost of the SIF sends every 1,000 chunks
 * with the number of gaps, and the same first events from the IOP module.
 */
static bool debug;
module_param(debug, bool, 0444);
MODULE_PARM_DESC(debug, "log the first events of a stream and the send statistics");

struct ps2_sound {
	struct snd_card *card;
	struct snd_pcm *pcm;
	struct snd_pcm_substream *substream;	/* open stream, or NULL */
	spinlock_t lock;			/* the counters below */
	u32 ring;				/* IOP address of the ring */
	u32 ring_size;
	u32 sent;				/* bytes sent since prepare */
	u32 consumed;				/* bytes played since prepare */
	u32 reported;				/* consumed at the last period */
	bool running;
	bool start_pending;			/* the tasklet sends START */
	bool stop_pending;			/* the tasklet sends STOP */
	struct tasklet_struct tasklet;
	unsigned xruns;				/* underruns since load */
	unsigned gaps;				/* times the ring ran dry */
	unsigned sends;				/* chunks sent since load */
	u64 send_ns;				/* time in the SIF send, total */
	u64 send_max_ns;			/* the longest send */
	struct completion hello;
	u16 volume[2];
};

static struct ps2_sound *sound;

static const struct snd_pcm_hardware ps2_pcm_hardware = {
	.info = SNDRV_PCM_INFO_INTERLEAVED | SNDRV_PCM_INFO_BLOCK_TRANSFER |
		SNDRV_PCM_INFO_MMAP | SNDRV_PCM_INFO_MMAP_VALID |
		SNDRV_PCM_INFO_BATCH,
	.formats = SNDRV_PCM_FMTBIT_S16_LE,
	.rates = SNDRV_PCM_RATE_48000,
	.rate_min = 48000,
	.rate_max = 48000,
	.channels_min = 2,
	.channels_max = 2,
	.buffer_bytes_max = 16 * SPU2_CHUNK,
	.period_bytes_min = SPU2_CHUNK,
	.period_bytes_max = 8 * SPU2_CHUNK,
	/*
	 * At least four periods. A program may ask for two (SDL2 does), and
	 * ALSA lets it write only one buffer ahead of the played position,
	 * so the buffer is the whole slack it gets. Four periods of 1,024
	 * frames are 85 ms. The ring on the IOP adds no slack: it holds
	 * only what the program has written.
	 */
	.periods_min = 4,
	.periods_max = 16,
};

static int ps2_send(u32 op, const void *packet, size_t size)
{
	int err = sif_cmd_opt(SIF_CMD_SPU2, op, packet, size);

	if (err < 0)
		pr_err(DRV_NAME ": command %u failed with %d\n", op, err);

	return err;
}

/*
 * The tasklet: the stop, then chunks while the application is ahead and
 * the ring has room, then the start. The counters are read and written
 * under the lock, the sends happen without it. The tasklet is the only
 * writer of "sent".
 */
static void ps2_tasklet(unsigned long arg)
{
	struct ps2_sound *s = (struct ps2_sound *)arg;
	struct snd_pcm_substream *substream;
	unsigned long flags;

	spin_lock_irqsave(&s->lock, flags);
	if (s->stop_pending) {
		s->stop_pending = false;
		spin_unlock_irqrestore(&s->lock, flags);
		ps2_send(SPU2_OP_STOP, NULL, 0);
		spin_lock_irqsave(&s->lock, flags);
	}
	for (;;) {
		struct snd_pcm_runtime *runtime;
		u32 buffer_bytes, appl, offset;
		struct spu2_data data;
		u64 t0, ns;
		int err;

		substream = s->substream;
		if (!substream || !substream->runtime)
			break;
		runtime = substream->runtime;
		buffer_bytes = snd_pcm_lib_buffer_bytes(substream);
		appl = frames_to_bytes(runtime, runtime->control->appl_ptr);
		if (s->sent + SPU2_CHUNK > appl ||
		    s->sent - s->consumed + SPU2_CHUNK > s->ring_size)
			break;
		offset = s->sent % s->ring_size;
		data.offset = offset;
		data.size = SPU2_CHUNK;
		spin_unlock_irqrestore(&s->lock, flags);

		t0 = ktime_get_ns();
		err = sif_cmd_opt_data(SIF_CMD_SPU2, SPU2_OP_DATA,
			&data, sizeof(data), s->ring + offset,
			runtime->dma_area + s->sent % buffer_bytes, SPU2_CHUNK);
		ns = ktime_get_ns() - t0;

		spin_lock_irqsave(&s->lock, flags);
		if (err < 0) {
			pr_err_ratelimited(DRV_NAME ": data send failed with %d\n", err);
			break;
		}
		s->sent += SPU2_CHUNK;
		s->sends++;
		s->send_ns += ns;
		if (ns > s->send_max_ns)
			s->send_max_ns = ns;
		/* The cost of the SIF, once every 1,000 chunks (11 s) */
		if (debug && s->sends % 1000 == 0)
			pr_info(DRV_NAME ": %u chunks sent, %llu us each, %llu us at most, %u gaps\n",
				s->sends, div_u64(s->send_ns, s->sends * 1000),
				div_u64(s->send_max_ns, 1000), s->gaps);
	}
	if (s->start_pending) {
		s->start_pending = false;
		spin_unlock_irqrestore(&s->lock, flags);
		ps2_send(SPU2_OP_START, NULL, 0);
		return;
	}
	spin_unlock_irqrestore(&s->lock, flags);
}

/* Runs in the SIF interrupt, when the IOP module sends a command. */
static void ps2_sif_event(const struct sif_cmd_header *header, void *arg)
{
	struct ps2_sound *s = arg;
	const void *payload = sif_cmd_payload(header);

	switch (header->opt) {
	case SPU2_OP_HELLO: {
		const struct spu2_hello *hello = payload;

		s->ring = hello->ring;
		s->ring_size = hello->ring_size;
		complete(&s->hello);
		break;
	}
	case SPU2_OP_CONSUMED: {
		const struct spu2_consumed *consumed = payload;
		struct snd_pcm_substream *substream;
		bool period = false;

		spin_lock(&s->lock);
		substream = s->running ? s->substream : NULL;
		if (debug && consumed->consumed == SPU2_CHUNK)
			pr_info(DRV_NAME ": first report, %u sent, %s\n", s->sent,
				substream ? "running" : "not running");
		/* A report from before a stop can arrive after the restart. */
		if (substream && consumed->consumed <= s->sent) {
			s->consumed = consumed->consumed;
			if (s->consumed == s->sent)
				s->gaps++;
			if (s->consumed - s->reported >=
			    snd_pcm_lib_period_bytes(substream)) {
				s->reported = s->consumed;
				period = true;
			}
		}
		spin_unlock(&s->lock);

		if (period)
			snd_pcm_period_elapsed(substream);
		if (substream)
			tasklet_schedule(&s->tasklet);
		break;
	}
	default:
		pr_warn_once(DRV_NAME ": unknown command %u from the IOP\n",
			(unsigned)header->opt);
	}
}

static int ps2_pcm_open(struct snd_pcm_substream *substream)
{
	struct ps2_sound *s = snd_pcm_substream_chip(substream);
	struct snd_pcm_runtime *runtime = substream->runtime;
	int err;

	runtime->hw = ps2_pcm_hardware;
	err = snd_pcm_hw_constraint_step(runtime, 0,
		SNDRV_PCM_HW_PARAM_PERIOD_BYTES, SPU2_CHUNK);
	if (err < 0)
		return err;
	err = snd_pcm_hw_constraint_step(runtime, 0,
		SNDRV_PCM_HW_PARAM_BUFFER_BYTES, SPU2_CHUNK);
	if (err < 0)
		return err;

	s->substream = substream;
	return 0;
}

static int ps2_pcm_close(struct snd_pcm_substream *substream)
{
	struct ps2_sound *s = snd_pcm_substream_chip(substream);
	unsigned long flags;

	tasklet_kill(&s->tasklet);
	spin_lock_irqsave(&s->lock, flags);
	s->substream = NULL;
	spin_unlock_irqrestore(&s->lock, flags);
	return 0;
}

static int ps2_pcm_hw_params(struct snd_pcm_substream *substream,
	struct snd_pcm_hw_params *params)
{
	return snd_pcm_lib_malloc_pages(substream, params_buffer_bytes(params));
}

static int ps2_pcm_hw_free(struct snd_pcm_substream *substream)
{
	struct ps2_sound *s = snd_pcm_substream_chip(substream);

	tasklet_kill(&s->tasklet);
	return snd_pcm_lib_free_pages(substream);
}

/* Runs in process context, with the stream lock but interrupts on. */
static int ps2_pcm_prepare(struct snd_pcm_substream *substream)
{
	struct ps2_sound *s = snd_pcm_substream_chip(substream);
	unsigned long flags;

	tasklet_kill(&s->tasklet);
	spin_lock_irqsave(&s->lock, flags);
	if (substream->runtime->status->state == SNDRV_PCM_STATE_XRUN) {
		s->xruns++;
		if (debug)
			pr_info_ratelimited(DRV_NAME ": underrun %u: %u sent, %u played, %lu frames written\n",
				s->xruns, s->sent, s->consumed,
				(unsigned long)substream->runtime->control->appl_ptr);
	}
	s->running = false;
	s->start_pending = false;
	s->stop_pending = false;
	s->sent = 0;
	s->consumed = 0;
	s->reported = 0;
	spin_unlock_irqrestore(&s->lock, flags);

	/* The IOP forgets its ring and plays silence. */
	return ps2_send(SPU2_OP_STOP, NULL, 0);
}

/* Runs with the stream lock held and interrupts off: no sends here. */
static int ps2_pcm_trigger(struct snd_pcm_substream *substream, int cmd)
{
	struct ps2_sound *s = snd_pcm_substream_chip(substream);

	switch (cmd) {
	case SNDRV_PCM_TRIGGER_START:
	case SNDRV_PCM_TRIGGER_RESUME:
		spin_lock(&s->lock);
		s->running = true;
		s->start_pending = true;
		spin_unlock(&s->lock);
		break;
	case SNDRV_PCM_TRIGGER_STOP:
	case SNDRV_PCM_TRIGGER_SUSPEND:
		spin_lock(&s->lock);
		s->running = false;
		s->stop_pending = true;
		spin_unlock(&s->lock);
		break;
	default:
		return -EINVAL;
	}
	tasklet_schedule(&s->tasklet);

	return 0;
}

/*
 * The position: the bytes the IOP played, but never all of them. ALSA
 * declares an underrun when the position reaches what the program wrote,
 * and the program then stops and restarts the stream, which refills the
 * whole buffer. On this console a program stalls now and then on the
 * swap. With the last chunk held back, the hardware plays silence for
 * the length of the stall and the stream goes on when data comes. The
 * gaps are counted (the IOP reports a played total equal to the sent
 * total) and printed with the chunk statistics.
 */
static snd_pcm_uframes_t ps2_pcm_pointer(struct snd_pcm_substream *substream)
{
	struct ps2_sound *s = snd_pcm_substream_chip(substream);
	const u32 buffer_bytes = snd_pcm_lib_buffer_bytes(substream);
	u32 position = s->consumed;

	if (!xrun && s->sent > 0 && position >= s->sent)
		position = s->sent - SPU2_CHUNK;

	return bytes_to_frames(substream->runtime, position % buffer_bytes);
}

/* Runs when the application wrote samples, with the stream lock held. */
static int ps2_pcm_ack(struct snd_pcm_substream *substream)
{
	struct ps2_sound *s = snd_pcm_substream_chip(substream);

	tasklet_schedule(&s->tasklet);

	return 0;
}

static const struct snd_pcm_ops ps2_pcm_ops = {
	.open = ps2_pcm_open,
	.close = ps2_pcm_close,
	.ioctl = snd_pcm_lib_ioctl,
	.hw_params = ps2_pcm_hw_params,
	.hw_free = ps2_pcm_hw_free,
	.prepare = ps2_pcm_prepare,
	.trigger = ps2_pcm_trigger,
	.pointer = ps2_pcm_pointer,
	.ack = ps2_pcm_ack,
};

/* --- the volume control --------------------------------------------------- */

#define PS2_VOLUME_MAX 0x3fff

static int ps2_volume_info(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_info *info)
{
	info->type = SNDRV_CTL_ELEM_TYPE_INTEGER;
	info->count = 2;
	info->value.integer.min = 0;
	info->value.integer.max = PS2_VOLUME_MAX;
	return 0;
}

static int ps2_volume_get(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *value)
{
	struct ps2_sound *s = snd_kcontrol_chip(kcontrol);

	value->value.integer.value[0] = s->volume[0];
	value->value.integer.value[1] = s->volume[1];
	return 0;
}

static int ps2_volume_put(struct snd_kcontrol *kcontrol,
	struct snd_ctl_elem_value *value)
{
	struct ps2_sound *s = snd_kcontrol_chip(kcontrol);
	const struct spu2_volume volume = {
		.left = min_t(long, value->value.integer.value[0], PS2_VOLUME_MAX),
		.right = min_t(long, value->value.integer.value[1], PS2_VOLUME_MAX),
	};

	if (volume.left == s->volume[0] && volume.right == s->volume[1])
		return 0;
	s->volume[0] = volume.left;
	s->volume[1] = volume.right;
	ps2_send(SPU2_OP_VOLUME, &volume, sizeof(volume));
	return 1;
}

static const struct snd_kcontrol_new ps2_volume_control = {
	.iface = SNDRV_CTL_ELEM_IFACE_MIXER,
	.name = "Master Playback Volume",
	.info = ps2_volume_info,
	.get = ps2_volume_get,
	.put = ps2_volume_put,
};

/* --- the card ------------------------------------------------------------- */

static int ps2_sound_probe(struct platform_device *pdev)
{
	struct snd_card *card;
	struct ps2_sound *s;
	int err;

	err = snd_card_new(&pdev->dev, SNDRV_DEFAULT_IDX1, "PS2", THIS_MODULE,
		sizeof(*s), &card);
	if (err < 0)
		return err;
	s = card->private_data;
	s->card = card;
	spin_lock_init(&s->lock);
	tasklet_init(&s->tasklet, ps2_tasklet, (unsigned long)s);
	init_completion(&s->hello);
	s->volume[0] = PS2_VOLUME_MAX;
	s->volume[1] = PS2_VOLUME_MAX;
	strcpy(card->driver, DRV_NAME);
	strcpy(card->shortname, "PS2 SPU2");
	strcpy(card->longname, "PlayStation 2 SPU2 sound processor");

	err = snd_pcm_new(card, "SPU2", 0, 1, 0, &s->pcm);
	if (err < 0)
		goto err_card;
	s->pcm->private_data = s;
	strcpy(s->pcm->name, "SPU2 sound input");
	snd_pcm_set_ops(s->pcm, SNDRV_PCM_STREAM_PLAYBACK, &ps2_pcm_ops);
	snd_pcm_lib_preallocate_pages_for_all(s->pcm, SNDRV_DMA_TYPE_CONTINUOUS,
		snd_dma_continuous_data(GFP_KERNEL),
		ps2_pcm_hardware.buffer_bytes_max,
		ps2_pcm_hardware.buffer_bytes_max);

	err = snd_ctl_add(card, snd_ctl_new1(&ps2_volume_control, s));
	if (err < 0)
		goto err_card;

	/* The IOP side: the ROM sound library, then the module */
	err = sif_request_cmd(SIF_CMD_SPU2, ps2_sif_event, s);
	if (err < 0)
		goto err_card;
	err = iop_module_load_rom("rom0:LIBSD");
	if (err < 0)
		dev_warn(&pdev->dev, "rom0:LIBSD gave %d (already loaded?)\n", err);
	err = iop_module_request("spu2", 0x0100, debug ? "debug" : NULL);
	if (err < 0) {
		dev_err(&pdev->dev, "IOP module spu2 failed with %d\n", err);
		goto err_sif;
	}
	if (!wait_for_completion_timeout(&s->hello, HZ)) {
		dev_err(&pdev->dev, "no answer from the IOP module\n");
		err = -ETIMEDOUT;
		goto err_sif;
	}

	err = snd_card_register(card);
	if (err < 0)
		goto err_sif;

	platform_set_drvdata(pdev, s);
	sound = s;
	dev_info(&pdev->dev, "SPU2 through spu2.irx, ring of %u bytes\n",
		s->ring_size);
	return 0;

err_sif:
	sif_request_cmd(SIF_CMD_SPU2, NULL, NULL);
err_card:
	snd_card_free(card);
	return err;
}

static int ps2_sound_remove(struct platform_device *pdev)
{
	struct ps2_sound *s = platform_get_drvdata(pdev);

	tasklet_kill(&s->tasklet);
	ps2_send(SPU2_OP_STOP, NULL, 0);
	sif_request_cmd(SIF_CMD_SPU2, NULL, NULL);
	snd_card_free(s->card);
	sound = NULL;
	return 0;
}

static struct platform_driver ps2_sound_driver = {
	.probe = ps2_sound_probe,
	.remove = ps2_sound_remove,
	.driver = {
		.name = DRV_NAME,
	},
};

static struct platform_device *ps2_sound_device;

static int __init ps2_sound_init(void)
{
	int err;

	err = platform_driver_register(&ps2_sound_driver);
	if (err < 0)
		return err;
	ps2_sound_device = platform_device_register_simple(DRV_NAME, -1, NULL, 0);
	if (IS_ERR(ps2_sound_device)) {
		platform_driver_unregister(&ps2_sound_driver);
		return PTR_ERR(ps2_sound_device);
	}
	return 0;
}

static void __exit ps2_sound_exit(void)
{
	platform_device_unregister(ps2_sound_device);
	platform_driver_unregister(&ps2_sound_driver);
}

module_init(ps2_sound_init);
module_exit(ps2_sound_exit);

MODULE_DESCRIPTION("PlayStation 2 SPU2 sound");
MODULE_LICENSE("GPL");
