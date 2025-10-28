import os
import sys
# 添加 lib/Python 目录到路径
sys.path.insert(0, os.path.join(os.path.dirname(__file__), '../../../lib/Python'))

print(os.listdir('.'))
import cv2
import threading
import time
import numpy as np
from typing import Optional, Tuple, List
import queue
import signal
import hv_event_reader_python as hvr
import hv_evt2_codec_python as hvc
from metavision_core.event_io import EventsIterator
from metavision_sdk_core import PeriodicFrameGenerationAlgorithm

# Global flag for running state
g_running = True


def signal_handler(signum, frame):
    global g_running
    g_running = False


class EventPlayer:
    def __init__(self):
        # Initialize variables in the exact same order as C++ version
        self.reader = hvr.HVEventReader()  # 1
        self.frame_gen = None
        self.play_thread = None
        self.is_playing = False  # 2
        self.width = 0
        self.height = 0
        self.total_events = 0
        self.start_time = 0
        self.end_time = 0
        self.duration_us = 0
        self.current_time = 0  # 3
        self.last_frame_time = 0  # 6
        self.playback_speed = 1.0  # 4
        self.batch_size = 10000  # 5
        self.frame_mutex = threading.Lock()
        self.display_frame = None
        self.frame_ready = False

    def open(self, filename: str) -> bool:
        try:
            # Initialize the event reader (replace with your actual reader)
            result = self.reader.open(filename)


            # Get file information
            self.width, self.height = self.reader.get_image_size()

            temp_header = self.reader.get_header()
            self.start_time = temp_header.start_timestamp

            print("File information:")
            print(f"  Resolution: {self.width}x{self.height} start_time:{self.start_time}")
            accumulation_time_us = 50000
            fps = 20
            # Initialize frame generator
            self.frame_gen = PeriodicFrameGenerationAlgorithm(
                self.width,
                self.height,
                accumulation_time_us,
                fps
            )

            # Set frame generator callback
            def frame_callback(ts, frame):
                with self.frame_mutex:
                    if frame is not None and frame.size > 0:
                        self.display_frame = frame.copy()
                        self.current_time = ts + self.start_time
                        self.frame_ready = True

            self.frame_gen.set_output_callback(frame_callback)

            return True

        except Exception as e:
            print(f"Failed to open event file: {e}")
            return False

    def start(self):
        if self.is_playing:
            return

        self.is_playing = True
        self.play_thread = threading.Thread(target=self._playback_loop, daemon=True)
        self.play_thread.start()

    def stop(self):
        if not self.is_playing:
            return

        self.is_playing = False
        if self.play_thread is not None:
            self.play_thread.join()

    def is_playing(self) -> bool:
        return self.is_playing

    def has_frame(self) -> bool:
        with self.frame_mutex:
            return self.frame_ready and self.display_frame is not None

    def get_frame(self) -> Optional[np.ndarray]:
        with self.frame_mutex:
            if self.display_frame is None:
                return None
            return self.display_frame.copy()


    def _playback_loop(self):
        last_event_time = None
        while self.is_playing and g_running:
            try:
                read_count, events = self.reader.read_events(self.batch_size)
                if read_count == 0 or events.size == 0:
                    break

                # Process events
                self.frame_gen.process_events(events)

                # Calculate wait time based on event timestamps
                current_event_time = events[-1][3]
                if last_event_time is not None:
                    time_diff_us = (current_event_time - last_event_time) / self.playback_speed
                    if time_diff_us > 0:
                        # Cap maximum sleep time to avoid freezing
                        time.sleep(min(time_diff_us / 1e6, 0.1))  # Max 100ms

                last_event_time = current_event_time

            except Exception as e:
                print(f"Error during playback: {e}")
                break


def main():
    # Register signal handler
    global g_running

    signal.signal(signal.SIGINT, signal_handler)

    # Parse command line arguments
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <event_file.raw>")
        return -1

    input_file = sys.argv[1]

    # Create player instance
    player = EventPlayer()
    if not player.reader:
        print("error-1\n")
        return -1;
    if not player.open(input_file):
        return -1

    # Create OpenCV window
    window_name = f"HV Event Player - {input_file}"
    cv2.namedWindow(window_name, cv2.WINDOW_AUTOSIZE)

    # Start playback
    player.start()

    # Main loop
    while g_running:
        if player.has_frame():
            event_frame = player.get_frame()
            if event_frame is not None:
            	cv2.imshow(window_name, event_frame)

        key = cv2.waitKey(1) & 0xFF
        if key in (27, ord('q'), ord('Q')):
            g_running = False

        if not player.is_playing:
            last_frame = player.get_frame()
            if last_frame is not None:
                cv2.putText(last_frame, "press the Q key to exit", (20, 30),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 255), 2)
                cv2.imshow(window_name, last_frame)

            key = cv2.waitKey(30) & 0xFF
            if key in (27, ord('q'), ord('Q')):
                g_running = False

    player.stop()
    cv2.destroyAllWindows()
    print("Program exited")
    return 0


if __name__ == "__main__":
    main()
