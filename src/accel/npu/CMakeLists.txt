# Qt-free cycle model of the systolic-array tensor unit (NPU). Built as a
# separate static library so that it can be unit tested without any widgets;
# the Qt peripheral wrapper lives in src/io/iotensorunit.*.
create_ripes_lib(accel_npu LINK_TO_RIPES_LIB)
