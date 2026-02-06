/**
 * @file news_service.cc
 * @brief 热点新闻服务实现（freejk 热榜 API）
 */

#include "gui_services/news_service.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkRequest>
#include <QUrl>
#include <QTimer>
#include <spdlog/spdlog.h>
#include "config/config.h"

namespace {
// 每个来源最多保留的标题数量，避免过长
constexpr int kMaxTitlesPerSource = 10;
// 滚动展示总量上限
constexpr int kMaxTotalTitles = 30;
// 单个来源请求超时（毫秒）
constexpr int kRequestTimeoutMs = 2000;
}  // namespace

NewsService* NewsService::instance_ = nullptr;

NewsService* NewsService::instance() {
    if (!instance_) {
        instance_ = new NewsService();
    }
    return instance_;
}

NewsService::NewsService(QObject* parent)
    : QObject(parent)
    , network_manager_(new QNetworkAccessManager(this))
    , has_cache_(false)
    , current_index_(0) {
    sources_.clear();
    for (int i = 0; i < Config::UI::NEWS_SOURCES_COUNT; ++i) {
        sources_.push_back(QString::fromUtf8(Config::UI::NEWS_SOURCES[i]));
    }
    per_source_titles_.resize(sources_.size());
}

void NewsService::setSources(const QStringList& sources) {
    if (!sources.isEmpty()) {
        sources_ = sources;
        per_source_titles_.assign(sources_.size(), {});
        current_index_ = 0;
    }
}

void NewsService::requestHeadlines() {
    collected_.clear();
    for (auto& lst : per_source_titles_) {
        lst.clear();
    }
    current_index_ = 0;
    fetchNext();
}

void NewsService::fetchNext() {
    if (current_index_ >= sources_.size()) {
        emitIfReady();
        return;
    }

    const int source_index = current_index_;
    const QString source = sources_[source_index];
    QUrl url(QString("%1%2").arg(Config::UI::NEWS_API_BASE, source));

    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "FaceRecognitionApp/1.0");

    // 取消已有请求
    if (current_reply_) {
        current_reply_->abort();
        current_reply_->deleteLater();
        current_reply_.clear();
    }

    current_reply_ = network_manager_->get(request);
    QPointer<QNetworkReply> reply = current_reply_;

    // 超时自动中断，避免卡住后续来源
    QTimer* timer = new QTimer(this);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, this, [this, reply, source_index]() {
        if (reply && reply->isRunning()) {
            spdlog::warn("News source {} timeout", sources_.value(source_index).toStdString());
            reply->abort();
        }
    });
    timer->start(kRequestTimeoutMs);

    connect(current_reply_, &QNetworkReply::finished, this, [this, timer]() {
        if (timer) {
            timer->stop();
            timer->deleteLater();
        }

        // reply 可能在析构后被置空
        QPointer<QNetworkReply> reply = current_reply_;
        int idx = current_index_;
        current_index_++;
        if (reply) {
            handleFinished(reply, idx);
            reply->deleteLater();
            current_reply_.clear();
        }
        fetchNext();  // 无论成功失败都继续下一个
    });
}

void NewsService::handleFinished(QNetworkReply* reply, int source_index) {
    if (reply->error() != QNetworkReply::NoError) {
        spdlog::warn("News source {} failed: {}", sources_.value(source_index).toStdString(),
                     reply->errorString().toStdString());
        return;
    }

    auto titles = parseTitles(reply);
    const int source_count = static_cast<int>(per_source_titles_.size());
    if (!titles.isEmpty() && source_index >= 0 && source_index < source_count) {
        per_source_titles_[source_index] = titles;
    }
}

QStringList NewsService::parseTitles(QNetworkReply* reply) const {
    QByteArray data = reply->readAll();
    QJsonDocument doc = QJsonDocument::fromJson(data);
    if (doc.isNull() || !doc.isObject()) {
        return {};
    }
    QJsonObject root = doc.object();
    QJsonArray arr = root["data"].toArray();

    QStringList list;
    list.reserve(kMaxTitlesPerSource);

    for (const auto& item : arr) {
        if (!item.isObject()) continue;
        QString title = item.toObject()["title"].toString().trimmed();
        if (title.isEmpty()) continue;
        list.push_back(title);
        if (list.size() >= kMaxTitlesPerSource) break;
    }
    return list;
}

void NewsService::emitIfReady() {
    collected_ = interleave();
    if (!collected_.isEmpty()) {
        cached_ = collected_;
        has_cache_ = true;
        emit headlinesUpdated(collected_);
        spdlog::info("NewsService updated headlines: {} items", collected_.size());
        return;
    }

    if (has_cache_) {
        emit headlinesUpdated(cached_);
        spdlog::warn("NewsService emits cached headlines due to empty fetch result");
    } else {
        emit errorOccurred("News request failed");
        spdlog::warn("NewsService no headlines and no cache");
    }
}

QString NewsService::sourceLabel(int index) const {
    if (index >= 0 && index < Config::UI::NEWS_SOURCES_COUNT) {
        return QString::fromUtf8(Config::UI::NEWS_SOURCE_LABELS[index]);
    }
    if (index >= 0 && index < sources_.size()) {
        return sources_[index];
    }
    return QStringLiteral("来源");
}

QString NewsService::sourceColor(int index) const {
    if (index >= 0 && index < Config::UI::NEWS_SOURCES_COUNT) {
        return QString::fromUtf8(Config::UI::NEWS_SOURCE_COLORS[index]);
    }
    return QStringLiteral("#1890ff");
}

QStringList NewsService::interleave() const {
    QStringList result;
    int remaining = kMaxTotalTitles;
    std::vector<int> offsets(per_source_titles_.size(), 0);

    while (remaining > 0) {
        bool added_in_round = false;
        for (int i = 0; i < static_cast<int>(per_source_titles_.size()); ++i) {
            const auto& lst = per_source_titles_[i];
            int& off = offsets[i];
            if (off < lst.size()) {
                QString text = lst.at(off).toHtmlEscaped();
                QString label = sourceLabel(i).toHtmlEscaped();
                QString color = sourceColor(i);
                QString labeled = QString("<span style=\"color:%1;font-weight:700;\">[%2]</span> %3")
                                      .arg(color, label, text);
                result.push_back(labeled);
                off++;
                remaining--;
                added_in_round = true;
                if (remaining == 0) break;
            }
        }
        if (!added_in_round) break;  // 所有列表都耗尽
    }
    return result;
}

