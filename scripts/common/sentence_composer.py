import json
import re
import sys
import urllib.error
import urllib.request


DAY_WORDS = ("今天", "昨天", "现在", "明天")
DAY_PERIOD_WORDS = ("早上", "中午", "下午", "晚上")
TIME_WORDS = (*DAY_WORDS, *DAY_PERIOD_WORDS)
NUMBER_WORDS = tuple(str(index) for index in range(1, 10))
REPEATABLE_TOKENS = set(NUMBER_WORDS) | {"小时", "天", "次"}
FILLER_TOKENS = {"我", "一起", "可以", "发生"}

DIRECT_SYMPTOMS = {
    "发烧": ("发烧", ("发烧", "发热")),
    "咳嗽": ("咳嗽", ("咳嗽",)),
    "拉肚子": ("拉肚子", ("拉肚子", "腹泻")),
    "流鼻涕": ("流鼻涕", ("流鼻涕",)),
}

COMPOUND_SYMPTOMS = (
    (("头", "疼痛"), "头疼", ("头疼", "头痛")),
    (("鼻", "疼痛"), "鼻子疼", ("鼻子疼", "鼻痛")),
    (("鼻", "流血"), "鼻子流血", ("鼻子流血", "流鼻血", "鼻血", "鼻出血")),
    (("头", "流血"), "头部流血", ("头部流血", "头流血")),
)

GENERIC_SYMPTOMS = {
    "疼痛": ("疼痛", ("疼", "痛")),
    "流血": ("流血", ("流血",)),
}

PAIR_IGNORE_TOKENS = set(TIME_WORDS) | {"我", "一起", "可以", "发生", "没有", "不", "经常"} | set(NUMBER_WORDS) | {"小时", "天", "次", "到"}


def compact_tokens(tokens):
    compact = []
    for token in tokens:
        if token and (token not in compact or token in REPEATABLE_TOKENS):
            compact.append(token)
    return compact


def compact_symptom_tokens(tokens):
    return [token for token in compact_tokens(tokens) if token not in PAIR_IGNORE_TOKENS]


def has_adjacent_pair(tokens, first, second):
    symptom_tokens = compact_symptom_tokens(tokens)
    return any(
        symptom_tokens[index] == first and symptom_tokens[index + 1] == second
        for index in range(len(symptom_tokens) - 1)
    )


def time_phrase(tokens):
    if "到" in tokens:
        time_positions = [(index, token) for index, token in enumerate(tokens) if token in TIME_WORDS]
        if len(time_positions) >= 2:
            before = [item for item in time_positions if item[0] < tokens.index("到")]
            after = [item for item in time_positions if item[0] > tokens.index("到")]
            if before and after:
                return f"从{before[-1][1]}到{after[0][1]}"
    for index, token in enumerate(tokens[:-1]):
        if token in DAY_WORDS and tokens[index + 1] in DAY_PERIOD_WORDS:
            return f"{token}{tokens[index + 1]}"
    for token in tokens:
        if token in TIME_WORDS:
            return token
    return ""


def modifier_phrase(tokens):
    token_set = set(tokens)
    negative = "不" in token_set or "没有" in token_set
    if negative and "经常" in token_set:
        return "不经常"
    if negative:
        return "没有"
    if "经常" in token_set:
        return "经常"
    return ""


def decorate_symptom(symptom, modifier):
    if modifier == "不经常":
        return f"不经常{symptom}"
    if modifier == "没有":
        return f"没有{symptom}"
    if modifier == "经常":
        return f"经常{symptom}"
    return symptom


def symptom_slots(tokens):
    token_set = set(tokens)
    slots = []
    consumed = set()

    for required_tokens, phrase, allowed_outputs in COMPOUND_SYMPTOMS:
        first, second = required_tokens
        if has_adjacent_pair(tokens, first, second):
            slots.append({"phrase": phrase, "allowed": allowed_outputs})
            consumed.update(required_tokens)

    for token, (phrase, allowed_outputs) in DIRECT_SYMPTOMS.items():
        if token in token_set:
            slots.append({"phrase": phrase, "allowed": allowed_outputs})
            consumed.add(token)

    for token, (phrase, allowed_outputs) in GENERIC_SYMPTOMS.items():
        if token in token_set and token not in consumed:
            slots.append({"phrase": phrase, "allowed": allowed_outputs})

    return slots


def symptom_phrases(tokens):
    modifier = modifier_phrase(tokens)
    return [decorate_symptom(slot["phrase"], modifier) for slot in symptom_slots(tokens)]


def time_segments(tokens):
    segments = []
    current_time = ""
    current_tokens = []
    index = 0
    while index < len(tokens):
        token = tokens[index]
        if token in TIME_WORDS:
            if current_time or current_tokens:
                segments.append((current_time, current_tokens))
            if token in DAY_WORDS and index + 1 < len(tokens) and tokens[index + 1] in DAY_PERIOD_WORDS:
                current_time = f"{token}{tokens[index + 1]}"
                index += 2
                current_tokens = []
                continue
            current_time = token
            current_tokens = []
        else:
            current_tokens.append(token)
        index += 1
    if current_time or current_tokens:
        segments.append((current_time, current_tokens))
    return segments


def duration_phrase(tokens):
    for index, token in enumerate(tokens[:-1]):
        if token in NUMBER_WORDS and tokens[index + 1] in ("小时", "天", "次"):
            return f"{token}{tokens[index + 1]}"
    return ""


def extract_slots(tokens):
    compact = compact_tokens(tokens)
    return {
        "tokens": compact,
        "subject": "我",
        "time": time_phrase(compact),
        "duration": duration_phrase(compact),
        "modifier": modifier_phrase(compact),
        "symptoms": symptom_slots(compact),
    }


def format_symptoms(symptom_slots_value, modifier):
    return [
        decorate_symptom(slot["phrase"], modifier)
        for slot in symptom_slots_value
    ]


def join_symptoms(symptoms, token_set):
    joined = "、".join(symptoms)
    if "一起" in token_set and len(symptoms) >= 2:
        return f"同时{joined}"
    return joined


def compose_tokens_rule(tokens):
    slots = extract_slots(tokens)
    compact = slots["tokens"]
    if not compact:
        return ""

    token_set = set(compact)
    subject = slots["subject"]
    time_word = slots["time"]
    symptoms = format_symptoms(slots["symptoms"], slots["modifier"])
    duration = slots["duration"]

    timed_parts = []
    if "到" not in token_set:
        for segment_time, segment_tokens in time_segments(compact):
            segment_slots = extract_slots(segment_tokens)
            segment_symptoms = format_symptoms(segment_slots["symptoms"], segment_slots["modifier"])
            segment_duration = segment_slots["duration"]
            if segment_time and segment_symptoms:
                segment_text = join_symptoms(segment_symptoms, set(segment_tokens))
                timed_parts.append(f"{segment_time}{segment_text}{segment_duration}")
            elif segment_time and segment_duration:
                timed_parts.append(f"{segment_time}{segment_duration}")

    if len(timed_parts) >= 2:
        return f"{subject}{'，'.join(timed_parts)}。"

    if timed_parts and not symptoms:
        return f"{subject}{timed_parts[0]}。"

    if symptoms:
        return f"{subject}{time_word}{join_symptoms(symptoms, token_set)}{duration}。"

    if time_word.startswith("从"):
        return f"{time_word}。"

    if "不知道" in token_set:
        return f"{subject}不知道。"
    if "你好" in token_set:
        return "你好。"
    if "谢谢" in token_set:
        return "谢谢。"

    useful = [token for token in compact if token not in FILLER_TOKENS]
    return "".join(useful or compact) + "。"


def normalize_medical_sentence(text):
    normalized = text.strip()
    replacements = {
        "头疼痛": "头疼",
        "头部疼痛": "头疼",
        "头痛痛": "头痛",
        "我头疼痛": "我头疼",
        "我昨天头疼痛": "我昨天头疼",
        "头发烧": "发烧",
        "头发热": "发热",
    }
    for src, dst in replacements.items():
        normalized = normalized.replace(src, dst)
    if normalized and normalized[-1] not in "。！？!?":
        normalized += "。"
    return normalized


def build_rkllm_prompt(tokens):
    compact = compact_tokens(tokens)
    token_text = " ".join(compact)
    rule_sentence = compose_tokens_rule(compact)
    return (
        "你是手语识别文本后处理模块，只负责润色句子。\n"
        "不要诊断，不要解释，不要聊天，不要提问。\n"
        "原始词汇可能不是标准语序，可以按保底句调整语序。\n"
        "必须保持原句中的时间、人称、身体部位、症状、否定和程度不变。\n"
        "禁止新增病情，禁止删除病情，禁止把一种症状改成另一种症状。\n"
        "只输出润色后的中文病情短句。\n"
        f"原始词汇：{token_text}\n"
        f"保底句：{rule_sentence}\n"
        "输出："
    )


def sentence_matches_tokens(sentence, tokens):
    slots = extract_slots(tokens)
    compact = slots["tokens"]
    token_set = set(compact)
    checks = []
    for token in TIME_WORDS:
        if token in token_set:
            checks.append((token,))
    if "到" in token_set:
        range_text = slots["time"]
        if range_text:
            checks.append((range_text,))
    checks.append((slots["subject"],))
    if "不" in token_set or "没有" in token_set:
        checks.append(("不", "没有"))
    if "经常" in token_set:
        checks.append(("经常",))
    for symptom in slots["symptoms"]:
        checks.append(symptom["allowed"])
    duration = slots["duration"]
    if duration:
        checks.append((duration,))

    return all(any(option in sentence for option in options) for options in checks)


def validate_llm_sentence(sentence, tokens=None):
    if not sentence:
        return False, "empty"
    forbidden = (
        "医生", "用户", "助手", "系统", "词汇", "词组", "主诉", "模板",
        "提供", "以下", "分别", "以及", "英文", "翻译", "think", "im_",
        "yesterday", "pain", "fever", "cough", "你好", "您好", "对吗", "是否",
    )
    forbidden_hit = next((word for word in forbidden if word in sentence), "")
    if forbidden_hit:
        return False, f"forbidden:{forbidden_hit}"
    if "?" in sentence or "？" in sentence or sentence.endswith("吗。"):
        return False, "question"
    if re.search(r"[A-Za-z]", sentence):
        return False, "english"
    if len(sentence) > 40:
        return False, "too_long"
    if tokens is not None and not sentence_matches_tokens(sentence, tokens):
        return False, "slot_mismatch"
    return True, "ok"


def is_valid_llm_sentence(sentence, tokens=None):
    valid, _ = validate_llm_sentence(sentence, tokens)
    return valid


def should_call_rkllm(tokens):
    slots = extract_slots(tokens)
    compact = slots["tokens"]
    if not compact:
        return False
    if slots["symptoms"]:
        return True
    if slots["duration"]:
        return True
    if slots["time"].startswith("从"):
        return True
    if any(token in compact for token in ("不知道", "你好", "谢谢")):
        return True
    useful = [token for token in compact if token not in FILLER_TOKENS]
    return len(useful) >= 2


def clean_rkllm_text(text):
    cleaned = text or ""
    if "</think>" in cleaned:
        cleaned = cleaned.split("</think>")[-1]

    cleaned = re.sub(r"<\|[^>]+?\|>", "", cleaned)
    cleaned = re.sub(r"</?[^>]+?>", "", cleaned)
    cleaned = cleaned.replace("用户", "\n")
    cleaned = cleaned.replace("assistant", "\n")
    cleaned = cleaned.replace("主诉：", "\n")
    cleaned = cleaned.replace("答案：", "\n")

    lines = [line.strip() for line in cleaned.splitlines() if line.strip()]
    if not lines:
        return ""

    candidate = lines[-1]
    candidate = re.sub(r"^[：:，,。.\s]+", "", candidate).strip()
    return normalize_medical_sentence(candidate)


def compose_tokens_rkllm(tokens, url, timeout):
    payload = {
        "model": "deepseek",
        "messages": [
            {
                "role": "user",
                "content": build_rkllm_prompt(tokens),
            }
        ],
        "stream": False,
    }
    data = json.dumps(payload, ensure_ascii=False).encode("utf-8")
    request = urllib.request.Request(
        url,
        data=data,
        headers={"Content-Type": "application/json"},
        method="POST",
    )

    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open(request, timeout=timeout) as response:
        body = response.read().decode("utf-8")

    parsed = json.loads(body)
    content = parsed["choices"][-1]["message"]["content"]
    return clean_rkllm_text(content)


def compose_sentence_with_source(tokens, backend="rule", rkllm_url=None, timeout=8.0):
    if backend == "rkllm" and rkllm_url and should_call_rkllm(tokens):
        try:
            sentence = compose_tokens_rkllm(tokens, rkllm_url, timeout)
            valid, reason = validate_llm_sentence(sentence, tokens)
            if valid:
                return normalize_medical_sentence(sentence), "rkllm"
            print(
                f"[composer] rkllm rejected: {reason}; sentence={sentence}; tokens={compact_tokens(tokens)}",
                file=sys.stderr,
                flush=True,
            )
        except (OSError, urllib.error.URLError, TimeoutError, KeyError, ValueError, json.JSONDecodeError) as exc:
            print(
                f"[composer] rkllm failed: {type(exc).__name__}: {exc}; tokens={compact_tokens(tokens)}",
                file=sys.stderr,
                flush=True,
            )
    return normalize_medical_sentence(compose_tokens_rule(tokens)), "rule_fallback" if backend == "rkllm" else "rule"


def compose_sentence(tokens, backend="rule", rkllm_url=None, timeout=8.0):
    sentence, _ = compose_sentence_with_source(tokens, backend, rkllm_url, timeout)
    return sentence
