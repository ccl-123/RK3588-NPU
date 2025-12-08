/**
 * @file news_service.h
 * @brief 热点新闻服务 - 从 freejk 热榜获取标题，供跑马灯滚动
 */

#pragma once

#include <QObject>
#include <QPointer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStringList>
#include <vector>

class NewsService : public QObject {
    Q_OBJECT
public:
    static NewsService* instance();

    // 主动拉取热点列表（按 sources 顺序依次请求，合并结果）
    void requestHeadlines();

    // 设置来源（默认：baidu, netease-news）
    void setSources(const QStringList& sources);

signals:
    void headlinesUpdated(const QStringList& headlines);
    void errorOccurred(const QString& message);

private:
    explicit NewsService(QObject* parent = nullptr);
    ~NewsService() = default;

    void fetchNext();
    void handleFinished(QNetworkReply* reply, int source_index);
    QStringList parseTitles(QNetworkReply* reply) const;
    QString sourceLabel(int index) const;
    QString sourceColor(int index) const;
    QStringList interleave() const;
    void emitIfReady();

    static NewsService* instance_;

    QNetworkAccessManager* network_manager_;
    QPointer<QNetworkReply> current_reply_;
    QStringList sources_;
    std::vector<QStringList> per_source_titles_;
    QStringList collected_;
    QStringList cached_;
    bool has_cache_;
    int current_index_;
};


