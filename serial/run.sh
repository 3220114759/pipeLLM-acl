export LD_PRELOAD=/data/lzy/serial/pipellm_serial.so
export PIPE_ENABLE_H2D=1
export PIPE_ENABLE_D2H=1
export PIPE_ENABLE_CHECK=1
export LD_LIBRARY_PATH=/data/lzy/pipellm_acl/build/lib:$LD_LIBRARY_PATH
# export ASCEND_RT_VISIBLE_DEVICES=4
# python3 /data/lzy/serial/test.py
# python3 /data/lzy/test.py
python3 /data/lzy/serial/off.py