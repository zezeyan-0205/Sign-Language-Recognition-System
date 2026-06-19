import json
import re
import urllib.error
import urllib.request


def compact_tokens(tokens):
    compact = []
    for token in tokens:
        if token and token not in compact:
            compact.append(token)
    return compact


def compose_tokens_rule(tokens):
    compact = compact_tokens(tokens)
    if not compact:
        return ""

    token_set = set(compact)
    subject = "我" if "我" in token_set else "患者"
    time_word = "昨天" if "昨天" in token_set else ""

    symptoms = []
    if "发烧" in token_set:
        symptoms.append("发烧")
    if "咳嗽" in token_set:
        symptoms.append("咳嗽")
    if "头" in token_set and "疼痛" in token_set:
        symptoms.append("头疼")

    if symptoms:
        return f"{subject}{time_word}{'、'.join(symptoms)}。"

    return "".join(compact) + "。"


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
        "把手语识别词整理成一句第一人称病情描述。\n"
        "只输出一句中文短句，不要解释，不要思考过程，不要模板标记。\n"
        "禁止输出这些词：医生、用户、助手、系统、词汇、主诉。\n"
        "如果有“昨天”，必须保留昨天。如果有“我”，必须用“我”作主语。\n"
        "“头”和“疼痛”同时出现时写成“头疼”，不要写“头疼痛”。\n"
        f"词汇：{token_text}\n"
        f"参考句：{rule_sentence}\n"
        "答案："
    )


def is_valid_llm_sentence(sentence):
    if not sentence:
        return False
    forbidden = (
        "医生", "用户", "助手", "系统", "词汇", "词组", "主诉", "模板",
        "提供", "以下", "分别", "以及", "英文", "翻译", "think", "im_",
        "yesterday", "pain", "fever", "cough",
    )
    if any(word in sentence for word in forbidden):
        return False
    if re.search(r"[A-Za-z]", sentence):
        return False
    if len(sentence) > 40:
        return False
    return True


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
    if backend == "rkllm" and rkllm_url:
        try:
            sentence = compose_tokens_rkllm(tokens, rkllm_url, timeout)
            if is_valid_llm_sentence(sentence):
                return normalize_medical_sentence(sentence), "rkllm"
        except (OSError, urllib.error.URLError, TimeoutError, KeyError, ValueError, json.JSONDecodeError):
            pass
    return normalize_medical_sentence(compose_tokens_rule(tokens)), "rule_fallback" if backend == "rkllm" else "rule"


def compose_sentence(tokens, backend="rule", rkllm_url=None, timeout=8.0):
    sentence, _ = compose_sentence_with_source(tokens, backend, rkllm_url, timeout)
    return sentence
