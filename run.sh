export LD_PRELOAD=/data/lzy/pipellm_acl/pipellm_acl.so
export PIPELLM_ENABLE_ENCRYPT=1
export PIPELLM_ENABLE_DECRYPT=1
export LD_LIBRARY_PATH=/data/lzy/pipellm_acl/build/lib:$LD_LIBRARY_PATH
export ASCEND_RT_VISIBLE_DEVICES=4
# python3 /data/lzy/pipellm_acl/test.py
# python3 /data/lzy/test.py
python3 /data/lzy/pipellm_acl/off.py