/**
 * @file incremental_response_parser.cc
 * @brief 增量解析 answer / think 标签
 */

#include "agent/incremental_response_parser.h"

namespace agent {

ParsedResponseDelta IncrementalResponseParser::consumeTaggedChunk(const QString& chunk,
                                                                  const QString& open_tag,
                                                                  const QString& close_tag,
                                                                  bool& in_tag,
                                                                  QString& pending,
                                                                  bool record_start,
                                                                  bool* started,
                                                                  bool* finished) {
    ParsedResponseDelta delta;
    pending += chunk;

    if (!in_tag) {
        const int pos = pending.indexOf(open_tag);
        if (pos >= 0) {
            in_tag = true;
            if (record_start && started) {
                *started = true;
            }
            const QString after = pending.mid(pos + open_tag.size());
            pending.clear();
            const int close_pos = after.indexOf(close_tag);
            if (close_pos >= 0) {
                delta.assistant_delta = after.left(close_pos);
                in_tag = false;
                if (finished) {
                    *finished = true;
                }
            } else {
                delta.assistant_delta = after;
            }
            return delta;
        }

        const int keep = qMax(0, open_tag.size() - 1);
        if (pending.size() > keep) {
            pending = pending.right(keep);
        }
        return delta;
    }

    const int pos = pending.indexOf(close_tag);
    if (pos >= 0) {
        delta.assistant_delta = pending.left(pos);
        pending.clear();
        in_tag = false;
        if (finished) {
            *finished = true;
        }
        return delta;
    }

    const int keep = qMax(0, close_tag.size() - 1);
    if (pending.size() > keep) {
        delta.assistant_delta = pending.left(pending.size() - keep);
        pending = pending.right(keep);
    }

    return delta;
}

ParsedResponseDelta IncrementalResponseParser::push(const QString& chunk) {
    bool answer_started = false;
    bool answer_finished = false;
    ParsedResponseDelta answer_delta = consumeTaggedChunk(
        chunk, "<answer>", "</answer>", in_answer_, answer_pending_, true,
        &answer_started, &answer_finished);
    answer_delta.answer_started = answer_started;
    answer_delta.answer_finished = answer_finished;

    if (!answer_delta.assistant_delta.isEmpty() || answer_delta.answer_started || answer_delta.answer_finished) {
        return answer_delta;
    }

    ParsedResponseDelta reasoning_delta = consumeReasoningChunk(chunk);
    answer_delta.reasoning_delta = reasoning_delta.reasoning_delta;
    return answer_delta;
}

ParsedResponseDelta IncrementalResponseParser::consumeReasoningChunk(const QString& chunk) {
    ParsedResponseDelta delta;
    reasoning_pending_ += chunk;

    if (!in_reasoning_) {
        const int think_pos = reasoning_pending_.indexOf("<think>");
        const int thought_pos = reasoning_pending_.indexOf("<thought>");

        int open_pos = -1;
        QString open_tag;
        if (think_pos >= 0 && (thought_pos < 0 || think_pos <= thought_pos)) {
            open_pos = think_pos;
            open_tag = "<think>";
            reasoning_close_tag_ = "</think>";
        } else if (thought_pos >= 0) {
            open_pos = thought_pos;
            open_tag = "<thought>";
            reasoning_close_tag_ = "</thought>";
        }

        if (open_pos >= 0) {
            in_reasoning_ = true;
            const QString after = reasoning_pending_.mid(open_pos + open_tag.size());
            reasoning_pending_.clear();
            const int close_pos = after.indexOf(reasoning_close_tag_);
            if (close_pos >= 0) {
                delta.reasoning_delta = after.left(close_pos);
                in_reasoning_ = false;
                reasoning_close_tag_ = "</think>";
            } else {
                delta.reasoning_delta = after;
            }
            return delta;
        }

        const int keep = QStringLiteral("<thought>").size() - 1;
        if (reasoning_pending_.size() > keep) {
            reasoning_pending_ = reasoning_pending_.right(keep);
        }
        return delta;
    }

    const int close_pos = reasoning_pending_.indexOf(reasoning_close_tag_);
    if (close_pos >= 0) {
        delta.reasoning_delta = reasoning_pending_.left(close_pos);
        reasoning_pending_.clear();
        in_reasoning_ = false;
        reasoning_close_tag_ = "</think>";
        return delta;
    }

    const int keep = qMax(0, reasoning_close_tag_.size() - 1);
    if (reasoning_pending_.size() > keep) {
        delta.reasoning_delta = reasoning_pending_.left(reasoning_pending_.size() - keep);
        reasoning_pending_ = reasoning_pending_.right(keep);
    }

    return delta;
}

void IncrementalResponseParser::reset() {
    in_answer_ = false;
    in_reasoning_ = false;
    answer_pending_.clear();
    reasoning_pending_.clear();
    reasoning_close_tag_ = "</think>";
}

}  // namespace agent
