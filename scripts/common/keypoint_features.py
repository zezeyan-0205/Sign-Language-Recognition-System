import numpy as np


SEQ_LEN = 30
POSE_DIM = 33 * 4
HAND_DIM = 21 * 3
RAW_FEATURE_DIM = POSE_DIM + HAND_DIM * 2

FEATURE_MODE_FULL = "full"
FEATURE_MODE_FULL_FACE_RELATIVE = "full_face_relative"
FEATURE_MODE_UPPER_BODY_HANDS_XY = "upper_body_hands_xy"
FEATURE_MODE_HANDS_XY = "hands_xy"
FEATURE_MODES = (
    FEATURE_MODE_FULL,
    FEATURE_MODE_FULL_FACE_RELATIVE,
    FEATURE_MODE_UPPER_BODY_HANDS_XY,
    FEATURE_MODE_HANDS_XY,
)

UPPER_BODY_POSE_INDICES = (11, 12, 13, 14, 15, 16)
FACE_REL_HAND_INDICES = (0, 4, 8, 12)
FACE_REL_DIM = 2 * len(FACE_REL_HAND_INDICES) * 4 * 3


def get_feature_dim(feature_mode):
    if feature_mode == FEATURE_MODE_FULL:
        return RAW_FEATURE_DIM
    if feature_mode == FEATURE_MODE_FULL_FACE_RELATIVE:
        return RAW_FEATURE_DIM + FACE_REL_DIM
    if feature_mode == FEATURE_MODE_UPPER_BODY_HANDS_XY:
        return len(UPPER_BODY_POSE_INDICES) * 3 + 21 * 2 * 2
    if feature_mode == FEATURE_MODE_HANDS_XY:
        return 21 * 2 * 2
    raise ValueError(f"Unsupported feature mode: {feature_mode}")


def normalize_keypoints(data):
    data = data.copy().astype(np.float32)

    for frame in data:
        pose = frame[:POSE_DIM].reshape(33, 4)
        left_shoulder = pose[11, :3]
        right_shoulder = pose[12, :3]

        if np.all(left_shoulder == 0) or np.all(right_shoulder == 0):
            continue

        center = (left_shoulder + right_shoulder) / 2.0
        shoulder_width = np.linalg.norm(left_shoulder[:2] - right_shoulder[:2])
        if shoulder_width < 1e-6:
            continue

        pose[:, :3] = (pose[:, :3] - center) / shoulder_width
        frame[:POSE_DIM] = pose.reshape(-1)

        left_start = POSE_DIM
        right_start = POSE_DIM + HAND_DIM
        left_hand = frame[left_start:right_start].reshape(21, 3)
        right_hand = frame[right_start:right_start + HAND_DIM].reshape(21, 3)

        if not np.all(left_hand == 0):
            frame[left_start:right_start] = (
                (left_hand - center) / shoulder_width
            ).reshape(-1)

        if not np.all(right_hand == 0):
            frame[right_start:right_start + HAND_DIM] = (
                (right_hand - center) / shoulder_width
            ).reshape(-1)

    return data


def valid_xy(point):
    return not np.allclose(point[:2], 0.0)


def average_valid(points):
    valid = [point[:2] for point in points if valid_xy(point)]
    if not valid:
        return None
    return np.mean(np.stack(valid, axis=0), axis=0)


def face_reference_points(pose_frame):
    nose = pose_frame[0, :2] if valid_xy(pose_frame[0]) else None
    eye_center = average_valid((pose_frame[2], pose_frame[5]))
    ear_center = average_valid((pose_frame[7], pose_frame[8]))
    head_center = average_valid(
        (pose_frame[0], pose_frame[2], pose_frame[5], pose_frame[7], pose_frame[8])
    )
    return (nose, eye_center, ear_center, head_center)


def hand_reference_points(hand_frame):
    if np.allclose(hand_frame, 0.0):
        return (None, None, None, None)
    wrist = hand_frame[0, :2] if valid_xy(hand_frame[0]) else None
    thumb_tip = hand_frame[4, :2] if valid_xy(hand_frame[4]) else None
    index_tip = hand_frame[8, :2] if valid_xy(hand_frame[8]) else None
    middle_tip = hand_frame[12, :2] if valid_xy(hand_frame[12]) else None
    return (wrist, thumb_tip, index_tip, middle_tip)


def face_relative_features(pose, left, right):
    """Append hand-to-face offsets for face-touch signs such as nose/head."""
    output = np.zeros((pose.shape[0], FACE_REL_DIM), dtype=np.float32)
    for frame_index in range(pose.shape[0]):
        face_refs = face_reference_points(pose[frame_index])
        hand_refs = (
            *hand_reference_points(left[frame_index]),
            *hand_reference_points(right[frame_index]),
        )
        values = []
        for hand_point in hand_refs:
            for face_point in face_refs:
                if hand_point is None or face_point is None:
                    values.extend((0.0, 0.0, 0.0))
                    continue
                delta = hand_point - face_point
                values.extend((delta[0], delta[1], np.linalg.norm(delta)))
        output[frame_index] = np.asarray(values, dtype=np.float32)
    return output


def select_features(normalized_data, feature_mode):
    data = np.asarray(normalized_data, dtype=np.float32)
    if data.ndim != 2 or data.shape[1] != RAW_FEATURE_DIM:
        raise ValueError(f"Expected (N, {RAW_FEATURE_DIM}), got {data.shape}")

    if feature_mode == FEATURE_MODE_FULL:
        return np.ascontiguousarray(data)

    pose = data[:, :POSE_DIM].reshape(data.shape[0], 33, 4)
    left = data[:, POSE_DIM:POSE_DIM + HAND_DIM].reshape(data.shape[0], 21, 3)
    right = data[:, POSE_DIM + HAND_DIM:POSE_DIM + HAND_DIM * 2].reshape(data.shape[0], 21, 3)

    if feature_mode == FEATURE_MODE_FULL_FACE_RELATIVE:
        features = [
            data,
            face_relative_features(pose, left, right),
        ]
        return np.ascontiguousarray(np.concatenate(features, axis=1).astype(np.float32))

    if feature_mode == FEATURE_MODE_UPPER_BODY_HANDS_XY:
        upper_pose_xyv = pose[:, UPPER_BODY_POSE_INDICES, :][:, :, [0, 1, 3]]
        features = [
            upper_pose_xyv.reshape(data.shape[0], -1),
            left[:, :, :2].reshape(data.shape[0], -1),
            right[:, :, :2].reshape(data.shape[0], -1),
        ]
        return np.ascontiguousarray(np.concatenate(features, axis=1).astype(np.float32))

    if feature_mode == FEATURE_MODE_HANDS_XY:
        features = [
            left[:, :, :2].reshape(data.shape[0], -1),
            right[:, :, :2].reshape(data.shape[0], -1),
        ]
        return np.ascontiguousarray(np.concatenate(features, axis=1).astype(np.float32))

    raise ValueError(f"Unsupported feature mode: {feature_mode}")


def prepare_sequence_array(data, feature_mode=FEATURE_MODE_FULL, seq_len=SEQ_LEN):
    data = np.asarray(data, dtype=np.float32)
    if data.ndim == 3:
        if data.shape[0] != 1:
            raise ValueError(f"Only batch=1 is supported, got {data.shape}")
        data = data[0]

    if data.ndim != 2 or data.shape[1] != RAW_FEATURE_DIM:
        raise ValueError(f"Expected (N, {RAW_FEATURE_DIM}), got {data.shape}")

    if data.shape[0] > seq_len:
        data = data[:seq_len]
    elif data.shape[0] < seq_len:
        padding = np.zeros((seq_len - data.shape[0], RAW_FEATURE_DIM), dtype=np.float32)
        data = np.concatenate((data, padding), axis=0)

    normalized = normalize_keypoints(data)
    return select_features(normalized, feature_mode)


def prepare_sequence_file(path, feature_mode=FEATURE_MODE_FULL, seq_len=SEQ_LEN):
    return prepare_sequence_array(np.load(path).astype(np.float32), feature_mode, seq_len)
