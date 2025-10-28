import sys
import os
# 添加 lib/Python 目录到路径
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../../lib/Python'))

import hv_evt2_codec_python as evt2
# 功能测试1：
# 创建一个EventCDEncoder类的实例
cd_encoder = evt2.EventCDEncoder()
# 参数1：x轴坐标，存储在bit12～21
# 参数2：y轴坐标，0～11
# 参数3：极性，存储在bit28～bit31
# 参数4：时间戳， bit22～bit27     
# 事件信息
cd_encoder.set_event(1024, 1024, 1, 32)   

# 准备4字节缓冲区
buffer =evt2.RawEvent()                                                                                    

# 将编码结果存放到buffer中
cd_encoder.encode(buffer)

# 查看编码结果
print(f"RawEvent: pad={buffer.pad:028b}, type={buffer.type:04b}") # 输出：'00000000 00000100 00100000 00001000'
# 以该输出结果为例，共4个字节32比特，其中，从左往右看各个数字在内存中的位置依次为 bit8 bit6 bit5 bit4 bit3 bit2 bit1 bit0 bit15~bit9 bit23~bit16 bit31~bit24

# 功能测试2：
trigger = evt2.EventTriggerEncoder()
# 参数1：极性，bit0
# 参数2：trigger_id，8～12
# 参数3：时间戳，22～27
# bit1~7：默认为0
# bit13～21：默认为0
# bit28~31：默认为0X0A
trigger.set_event(0, 5, 5)
buffer2 = evt2.RawEvent()   
trigger.encode(buffer2)
print(f"RawEvent: pad={buffer2.pad:028b}, type={buffer2.type:04b}")

# 将输入参数的bit3~bit0的值消为0，存入th
timer = evt2.EventTimeEncoder(16)
print("method1:get_next_time_high th = %d" % timer.get_next_time_high())
timer.reset(64)
print("method3 reset: th = %d" % timer.get_next_time_high())
buffer3 = evt2.RawEvent()
# 将th的bit6～bitx放到buffer3的bit0~bit27，然后内部的th加上2^4
timer.encode(buffer3) # 执行完该函数，th = 80
print(f"method2 encode: pad={buffer3.pad:028b}, type={buffer3.type:04b}")
timer.encode(buffer3) # 执行完该函数，th = 96
timer.encode(buffer3) # 执行完该函数，th = 112
timer.encode(buffer3) # 执行完该函数，th = 128
timer.encode(buffer3)
print(f"method2 encode: pad={buffer3.pad:028b}, type={buffer3.type:04b}")




