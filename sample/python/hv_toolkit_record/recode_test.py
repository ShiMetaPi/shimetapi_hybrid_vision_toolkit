import sys
import os
# 添加 lib/Python 目录到路径
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../../lib/Python'))

import cv2
import time
import signal
import numpy as np
import threading
from typing import List
from datetime import datetime
from dataclasses import dataclass
import hv_camera_python  # Assuming these are the Pybind11-generated modules
import hv_event_writer_python
from metavision_sdk_base import EventCD
# Global flag for recording control
g_recording = False


def signal_handler(signum, frame):
    global g_recording
    print("\nReceived stop signal, stopping recording...")
    g_recording = False


class EventRecorder:
    def __init__(self):
        self.event_writer = hv_event_writer_python.HVEventWriter()
        self.output_filename = ""
        self.total_events = 0
        self.last_flush_time = 0.0

    def start_recording(self, output_filename: str) -> bool:
        # Standard EV camera resolution constants should match your C++ defines
        HV_EVS_WIDTH = 768  # Replace with actual value from your C++ code
        HV_EVS_HEIGHT = 608  # Replace with actual value from your C++ code

        if not self.event_writer.open(output_filename, HV_EVS_WIDTH, HV_EVS_HEIGHT):
            print(f"Failed to create output file: {output_filename}")
            return False

        self.output_filename = output_filename
        self.total_events = 0
        self.last_flush_time = time.time()
        print(f"Started recording events to file: {output_filename}")
        return True

    def stop_recording(self):
        if self.event_writer and self.event_writer.is_open():
            self.event_writer.flush()
            print(f"Total events recorded: {self.total_events}")
            print(f"Event file size: {self.event_writer.get_file_size()} bytes")
            self.event_writer.close()
            print(f"Event file saved: {self.output_filename}")

    def on_event_received(self, events):
        global g_recording

        if not g_recording or not self.event_writer.is_open():
            return

        # Batch write events
        written = self.event_writer.write_events(events)
        self.total_events += written

        # Periodic buffer flushing (every second)
        now = time.time()
        if now - self.last_flush_time > 1.0:
            self.event_writer.flush()
            self.last_flush_time = now



class VideoRecorder:
    video_writer: cv2.VideoWriter = None
    output_filename: str = ""
    total_frames: int = 0
    fps: float = 30.0

    def start_recording(self, output_filename: str, fps: float = 30.0) -> bool:
        self.fps = fps
        self.output_filename = output_filename
        self.total_frames = 0

        # Standard APS resolution constants should match your C++ defines
        HV_APS_WIDTH = 768  # Replace with actual value from your C++ code
        HV_APS_HEIGHT = 608  # Replace with actual value from your C++ code

        fourcc = cv2.VideoWriter_fourcc(*'MJPG')
        self.video_writer = cv2.VideoWriter(
            output_filename,
            fourcc,
            fps,
            (HV_APS_WIDTH, HV_APS_HEIGHT)
        )

        if not self.video_writer.isOpened():
            print(f"Failed to create video file: {output_filename}")
            return False

        print(f"Started recording video to file: {output_filename}")
        return True

    def stop_recording(self):
        if self.video_writer and self.video_writer.isOpened():
            self.video_writer.release()
            print(f"Total frames recorded: {self.total_frames}")
            print(f"Video file saved: {self.output_filename}")

    def on_image_received(self, image):
        global g_recording

        if not g_recording or not self.video_writer.isOpened():
            return

        # Write video frame
        self.video_writer.write(image)
        self.total_frames += 1


class HVRecorder:
    def __init__(self):
        # Vendor and product IDs - should match your C++ defines
        self.VENDOR_ID = 0x1d6b
        self.PRODUCT_ID = 0x0105

        self.camera = hv_camera_python.HV_Camera(self.VENDOR_ID, self.PRODUCT_ID)
        self.event_recorder = EventRecorder()
        self.video_recorder = VideoRecorder()

        # Register signal handlers
        signal.signal(signal.SIGINT, signal_handler)
        signal.signal(signal.SIGTERM, signal_handler)

    def run(self, event_output_file="recorded_events.raw",
            video_output_file="recorded_video.avi",
            duration_seconds=15, fps=30.0):
        global g_recording

        print("=== HV Camera Event and Video Recording Example ===")
        print(f"Event output file: {event_output_file}")
        print(f"Video output file: {video_output_file}")
        print(f"Recording duration: {duration_seconds} seconds")
        print(f"Video frame rate: {fps} FPS")

        # Open camera
        print("\nOpening camera...")
        if not self.camera.open():
            print("Error: Failed to open HV camera")
            print("Please ensure:")
            print("1. Camera is properly connected to USB port")
            print("2. Camera drivers are properly installed")
            print("3. Camera is not being used by another program")
            return False

        print("Camera opened successfully!")

        # Start recording
        if not self.event_recorder.start_recording(event_output_file):
            self.camera.close()
            return False

        if not self.video_recorder.start_recording(video_output_file, fps):
            self.event_recorder.stop_recording()
            self.camera.close()
            return False

        # Start data acquisition
        print("\nStarting event and image data acquisition...")
        print("Press Ctrl+C to stop recording")

        g_recording = True

        # Event callback
        def event_callback(events):
            try:
                self.event_recorder.on_event_received(events)
            except Exception as e:
                print(f"Event processing error: {str(e)}")

        # Image callback
        def image_callback(image):
            try:
                self.video_recorder.on_image_received(image)
            except Exception as e:
                print(f"Image processing error: {str(e)}")

        # Start event capture
        print("Starting event capture...")
        if not self.camera.startEventCapture(event_callback):
            print("Error: Failed to start event capture")
            self.event_recorder.stop_recording()
            self.video_recorder.stop_recording()
            self.camera.close()
            return False

        # Start image capture
        print("Starting image capture...")
        if not self.camera.startImageCapture(image_callback):
            print("Error: Failed to start image capture")
            self.camera.stopEventCapture()
            self.event_recorder.stop_recording()
            self.video_recorder.stop_recording()
            self.camera.close()
            return False

        # Wait briefly to ensure acquisition has started
        time.sleep(0.5)

        # Record for specified duration or until interrupted
        start_time = time.time()

        while g_recording:
            time.sleep(0.1)

            elapsed = time.time() - start_time
            if elapsed >= duration_seconds:
                print(f"\nRecording reached {duration_seconds} seconds, stopping...")
                g_recording = False
                break

        # Stop acquisition
        print("\nStopping event and image acquisition...")
        self.camera.stopEventCapture()
        self.camera.stopImageCapture()

        # Wait briefly to ensure all data is processed
        time.sleep(0.5)

        # Stop recording and save files
        self.event_recorder.stop_recording()
        self.video_recorder.stop_recording()

        # Close camera
        self.camera.close()
        print("Camera closed")

        print("\n=== Recording Complete ===")
        print(f"Event file: {event_output_file}")
        print(f"Video file: {video_output_file}")
        print(f"Total events: {self.event_recorder.total_events}")
        print(f"Total video frames: {self.video_recorder.total_frames}")

        return True


if __name__ == "__main__":
    recorder = HVRecorder()
    recorder.run()