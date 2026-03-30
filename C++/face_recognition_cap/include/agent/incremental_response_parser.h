/**
 * @file incremental_response_parser.h
 * @brief 增量解析模型输出中的 answer / reasoning 区域
 */

#pragma once

#include <QString>
#include <QtGlobal>

namespace agent {

struct ParsedResponseDelta {
    QString assistant_delta;
    QString reasoning_delta;
    bool answer_started = false;
    bool answer_finished = false;
};

class IncrementalResponseParser {
public:
    ParsedResponseDelta push(const QString& chunk);
    void reset();

private:
    bool in_answer_ = false;
    bool in_reasoning_ = false;
    QString answer_pending_;
    QString reasoning_pending_;

    ParsedResponseDelta consumeTaggedChunk(const QString& chunk,
                                           const QString& open_tag,
                                           const QString& close_tag,
                                           bool& in_tag,
                                           QString& pending,
                                           bool record_start,
                                           bool* started,
                                           bool* finished);
};

}  // namespace agent
