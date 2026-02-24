/**
 * @file example_qt_http.cpp
 * @brief Qt-specific example for HTTP access with AsyncOp using QNetworkAccessManager
 * 
 * This example demonstrates how to integrate Qt's QNetworkAccessManager with AsyncOp
 * to create async HTTP operations that can be chained and composed like other AsyncOp operations.
 */

#include "async_op.hpp"
#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QTimer>
#include <QDebug>
#include <QString>
#include <QByteArray>
#include <QEventLoop>
#include <iostream>
#include <spdlog/spdlog.h>

// Structure to represent HTTP response
struct HttpResponse {
    int statusCode;
    QString headers;  // Simplified - in real usage might be QMap<QString, QString>
    QByteArray body;
    QString error;    // Empty if successful
};

// Convert QNetworkReply::NetworkError to our ErrorCode
ao::ErrorCode convertNetworkError(QNetworkReply::NetworkError error) {
    switch (error) {
        case QNetworkReply::NoError:
            return ao::ErrorCode::None;
        case QNetworkReply::TimeoutError:
        case QNetworkReply::OperationCanceledError:
            return ao::ErrorCode::Timeout;
        case QNetworkReply::ConnectionRefusedError:
        case QNetworkReply::RemoteHostClosedError:
        case QNetworkReply::HostNotFoundError:
            return ao::ErrorCode::NetworkError;
        default:
            return ao::ErrorCode::InvalidResponse;
    }
}

// Async HTTP GET request using QNetworkAccessManager
ao::AsyncOp<HttpResponse> httpGet(QNetworkAccessManager* manager, const QString& url) {
    spdlog::info("Making HTTP GET request to: {}", url.toStdString());
    
    ao::AsyncOp<HttpResponse> result;
    auto promise = result.m_promise;
    
    // Create network request
    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    // Make the network request
    QNetworkReply* reply = manager->get(request);
    
    // Connect to the finished signal
    QObject::connect(reply, &QNetworkReply::finished, [reply, promise]() {
        HttpResponse response;
        
        if (reply->error() == QNetworkReply::NoError) {
            // Successful response
            response.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            // Simplified headers representation
            QList<QByteArray> rawHeaders = reply->rawHeaderList();
            QStringList headerStrings;
            for (const QByteArray& header : rawHeaders) {
                headerStrings.append(QString("%1: %2")
                    .arg(QString(header))
                    .arg(QString(reply->rawHeader(header))));
            }
            response.headers = headerStrings.join("\n");
            response.body = reply->readAll();
            response.error = QString(); // Empty means no error
        } else {
            // Error response
            response.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            response.error = reply->errorString();
            spdlog::error("HTTP request failed: {}", response.error.toStdString());
        }
        
        // Resolve or reject the promise based on the response
        if (reply->error() == QNetworkReply::NoError) {
            spdlog::info("HTTP GET request successful, status: {}", response.statusCode);
            ao::invoke_main([promise, response = std::move(response)]() mutable {
                promise->resolveWith(std::move(response));
            });
        } else {
            spdlog::error("HTTP GET request failed: {}", response.error.toStdString());
            ao::invoke_main([promise, reply]() {
                promise->rejectWith(convertNetworkError(reply->error()));
            });
        }
        
        // Clean up the reply object
        reply->deleteLater();
    });
    
    // Also connect to error signal to catch other types of errors (using newer Qt5 syntax)
    QObject::connect(reply, &QNetworkReply::errorOccurred,
        [reply, promise](QNetworkReply::NetworkError error) {
            if (error != QNetworkReply::NoError) {
                spdlog::error("Network error in HTTP request: {}", static_cast<int>(error));
            }
        });
    
    return result;
}

// Async HTTP POST request using QNetworkAccessManager
ao::AsyncOp<HttpResponse> httpPost(QNetworkAccessManager* manager, const QString& url, const QByteArray& data) {
    spdlog::info("Making HTTP POST request to: {} with {} bytes", url.toStdString(), data.size());
    
    ao::AsyncOp<HttpResponse> result;
    auto promise = result.m_promise;
    
    // Create network request
    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    // Make the network request
    QNetworkReply* reply = manager->post(request, data);
    
    // Connect to the finished signal
    QObject::connect(reply, &QNetworkReply::finished, [reply, promise]() {
        HttpResponse response;
        
        if (reply->error() == QNetworkReply::NoError) {
            // Successful response
            response.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            response.body = reply->readAll();
            response.error = QString(); // Empty means no error
        } else {
            // Error response
            response.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            response.error = reply->errorString();
            spdlog::error("HTTP POST request failed: {}", response.error.toStdString());
        }
        
        // Resolve or reject the promise based on the response
        if (reply->error() == QNetworkReply::NoError) {
            spdlog::info("HTTP POST request successful, status: {}", response.statusCode);
            ao::invoke_main([promise, response = std::move(response)]() mutable {
                promise->resolveWith(std::move(response));
            });
        } else {
            spdlog::error("HTTP POST request failed: {}", response.error.toStdString());
            ao::invoke_main([promise, reply]() {
                promise->rejectWith(convertNetworkError(reply->error()));
            });
        }
        
        // Clean up the reply object
        reply->deleteLater();
    });
    
    return result;
}

// Example of chaining HTTP requests
void demonstrateHttpChaining(QNetworkAccessManager* manager) {
    spdlog::info("\n=== HTTP Request Chaining Example ===");
    
    // First, get user data
    httpGet(manager, "https://httpbin.org/get")
        .then([manager](const HttpResponse& response) {  // Capture manager by value
            spdlog::info("Step 1: Got response, status: {}, body length: {} bytes", 
                        response.statusCode, response.body.size());
            
            // Then make a POST request with the response data
            return httpPost(manager, "https://httpbin.org/post", response.body.left(100));
        })
        .then([](const HttpResponse& response) {
            spdlog::info("Step 2: POST response, status: {}, body length: {} bytes", 
                        response.statusCode, response.body.size());
        })
        .onError([](ao::ErrorCode error) {
            spdlog::error("HTTP request chain failed with error: {}", static_cast<int>(error));
        });
}

// Example of parallel HTTP requests
void demonstrateParallelHttp(QNetworkAccessManager* manager) {
    spdlog::info("\n=== Parallel HTTP Requests Example ===");
    
    // Create multiple HTTP requests
    auto request1 = httpGet(manager, "https://httpbin.org/delay/1");
    auto request2 = httpGet(manager, "https://httpbin.org/delay/1");
    auto request3 = httpGet(manager, "https://httpbin.org/delay/1");
    
    std::vector<ao::AsyncOp<HttpResponse>> requests = {request1, request2, request3};
    
    // Wait for all to complete
    ao::all(requests)
        .then([](const std::vector<HttpResponse>& responses) {
            spdlog::info("All {} HTTP requests completed", responses.size());
            for (size_t i = 0; i < responses.size(); ++i) {
                spdlog::info("  Response {}: status {}, body length {}", 
                            i + 1, responses[i].statusCode, responses[i].body.size());
            }
        })
        .onError([](ao::ErrorCode error) {
            spdlog::error("Some HTTP requests failed: {}", static_cast<int>(error));
        });
}

// Example of error handling with HTTP requests
void demonstrateHttpErrorHandling(QNetworkAccessManager* manager) {
    spdlog::info("\n=== HTTP Error Handling Example ===");
    
    // This URL should cause an error - demonstrate error handling
    httpGet(manager, "https://invalid-url-for-testing.com/nonexistent")
        .recover([](ao::ErrorCode error) -> HttpResponse {
            spdlog::warn("HTTP request failed as expected, using mock response. Error: {}", 
                        static_cast<int>(error));
            
            // Return a mock response when the real request fails
            HttpResponse mockResponse;
            mockResponse.statusCode = 200;
            mockResponse.body = "{\"message\": \"Mock response due to network error\"}";
            mockResponse.error = QString();
            return mockResponse;
        })
        .then([](const HttpResponse& response) {
            spdlog::info("Using recovered/mocked response: {}", response.body.constData());
        });
}

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    spdlog::set_level(spdlog::level::debug);
    
    spdlog::info("Qt HTTP AsyncOp Example");
    
    // Create a shared QNetworkAccessManager instance
    QNetworkAccessManager networkManager;
    
    // Run the different examples
    demonstrateHttpChaining(&networkManager);
    demonstrateParallelHttp(&networkManager);
    demonstrateHttpErrorHandling(&networkManager);
    
    // Also make a standalone request
    httpGet(&networkManager, "https://httpbin.org/json")
        .then([](const HttpResponse& response) {
            spdlog::info("Standalone request completed, status: {}, body preview: {:.50}", 
                        response.statusCode, response.body.constData());
        })
        .onError([](ao::ErrorCode error) {
            spdlog::error("Standalone request failed: {}", static_cast<int>(error));
        });
    
    spdlog::info("Starting Qt event loop...");
    
    // Run the Qt event loop to process network events
    return app.exec();
}