import argparse
import os
import time

import cv2


def parse_args():
    parser = argparse.ArgumentParser(
        description="Capture a single camera frame for MediaPipe smoke tests."
    )
    parser.add_argument("--camera", type=int, default=0, help="OpenCV camera index.")
    parser.add_argument("--width", type=int, default=1280, help="Capture width.")
    parser.add_argument("--height", type=int, default=720, help="Capture height.")
    parser.add_argument("--fps", type=int, default=30, help="Requested camera FPS.")
    parser.add_argument(
        "--output",
        type=str,
        default="artifacts/captures/pose_test.jpg",
        help="Output image path.",
    )
    parser.add_argument(
        "--fullscreen",
        action="store_true",
        help="Show preview window in fullscreen mode.",
    )
    parser.add_argument(
        "--warmup",
        type=int,
        default=10,
        help="Frames to discard before preview/capture.",
    )
    return parser.parse_args()


def put_text(frame, text, pos, scale=0.8, color=(255, 255, 255), thickness=2):
    cv2.putText(
        frame,
        text,
        pos,
        cv2.FONT_HERSHEY_SIMPLEX,
        scale,
        color,
        thickness,
        cv2.LINE_AA,
    )


def main():
    args = parse_args()

    cap = cv2.VideoCapture(args.camera)
    if not cap.isOpened():
        raise RuntimeError(f"Failed to open camera: {args.camera}")

    cap.set(cv2.CAP_PROP_FRAME_WIDTH, args.width)
    cap.set(cv2.CAP_PROP_FRAME_HEIGHT, args.height)
    cap.set(cv2.CAP_PROP_FPS, args.fps)

    for _ in range(max(args.warmup, 0)):
        cap.read()

    window_name = "Capture Pose Test Frame"
    cv2.namedWindow(window_name, cv2.WINDOW_NORMAL)
    if args.fullscreen:
        cv2.setWindowProperty(window_name, cv2.WND_PROP_FULLSCREEN, cv2.WINDOW_FULLSCREEN)

    os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)

    saved = False
    last_time = time.time()
    fps = 0.0

    print("====================================")
    print("Camera frame capture tool")
    print(f"Camera: {args.camera}")
    print(f"Requested: {args.width}x{args.height}@{args.fps}")
    print(f"Output: {args.output}")
    print("SPACE: save current frame")
    print("q/Esc: quit")
    print("====================================")

    while True:
        ok, frame = cap.read()
        if not ok:
            print("Failed to read frame.")
            break

        now = time.time()
        dt = now - last_time
        last_time = now
        if dt > 0:
            inst_fps = 1.0 / dt
            fps = inst_fps if fps <= 0 else 0.9 * fps + 0.1 * inst_fps

        display = frame.copy()
        put_text(display, "Stand back: show head, shoulders and arms", (30, 45))
        put_text(display, f"FPS: {fps:.1f}", (30, 85), scale=0.7)
        put_text(display, "SPACE: save   q/Esc: quit", (30, 125), scale=0.7)

        if saved:
            put_text(display, f"Saved: {args.output}", (30, 170), color=(0, 255, 0))

        cv2.imshow(window_name, display)
        key = cv2.waitKey(1) & 0xFF

        if key in (ord("q"), 27):
            break

        if key == 32:
            if cv2.imwrite(args.output, frame):
                saved = True
                print(f"Saved: {args.output}")
            else:
                print(f"Failed to save: {args.output}")

    cap.release()
    cv2.destroyAllWindows()


if __name__ == "__main__":
    main()
