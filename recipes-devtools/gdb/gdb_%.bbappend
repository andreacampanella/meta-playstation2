# gdb without Python scripting. With it, gdb pulls in python3, and python3
# pulls in libffi, which has no code for the single-float ABI of the
# r5900el-fpu tune. The debugger itself does not need Python.
PACKAGECONFIG:remove = "python"
