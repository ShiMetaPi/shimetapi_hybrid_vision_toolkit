import sys
import os
# 添加 lib/Python 目录到路径
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../../lib/Python'))

import hv_camera_python
import metavision_sdk_base
import cv2
import numpy as np
from threading import Event
# 1. 创建相机实例（替换为你的实际VID/PID）
camera = hv_camera_python.HV_Camera(0x1d6b, 0x0105)

# 2. 定义回调函数
def event_callback(events):
    """事件数据回调"""
    print(f"收到 {len(events)} 个事件")

def image_callback(img):
    """图像数据回调（img是numpy数组）"""
    print(f"Received image: %d,%d" %(img.shape[1],img.shape[0]))
# 3. 打开设备并启动采集
try:
    if camera.open():
        print("相机打开成功")
        
        # 启动采集（传入回调函数）
        camera.startEventCapture(event_callback)
        camera.startImageCapture(image_callback)
        
        # 4. 主循环（按q退出）
        print("采集已启动，按q键退出...")
        while True:
            if cv2.waitKey(1) & 0xFF == ord('q'):
                break
            
            # 也可以主动获取最新图像
            latest_img = camera.getLatestImage()
            if latest_img is not None:
                cv2.imshow("Latest Image", latest_img)
    
except KeyboardInterrupt:
    print("用户中断")
finally:
    # 5. 清理资源
    print("正在停止采集...")
    camera.stopEventCapture()
    camera.stopImageCapture()
    camera.close()
    cv2.destroyAllWindows()
    print("程序退出")
