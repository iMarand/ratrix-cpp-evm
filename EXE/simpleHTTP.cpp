#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QDateTime>

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    // Create and start server
    QTcpServer server;
    server.listen(QHostAddress::Any, 8080);
    qDebug() << "Server started on port 8080";
    
    // Store server start time
    QString startTime = QDateTime::currentDateTime().toString();
    
    // Handle new connections
    QObject::connect(&server, &QTcpServer::newConnection, [&server, startTime]() {
        QTcpSocket *socket = server.nextPendingConnection();
        
        // Handle incoming data
        QObject::connect(socket, &QTcpSocket::readyRead, [socket, startTime]() {
            QString request = QString::fromUtf8(socket->readAll());
            QStringList lines = request.split("\r\n");
            
            if (lines.isEmpty()) {
                socket->disconnectFromHost();
                return;
            }
            
            QStringList parts = lines[0].split(" ");
            if (parts.size() < 2) {
                socket->disconnectFromHost();
                return;
            }
            
            QString path = parts[1];
            QString content;
            
            // Simple routing
            if (path == "/" || path == "/index.html") {
                content = "<html><body><h1>Welcome to Qt6 Server</h1>"
                          "<p>Current time: " + QDateTime::currentDateTime().toString() + "</p>"
                          "<p><a href='/hello'>Hello</a> | <a href='/info'>Info</a></p>"
                          "</body></html>";
            }
            else if (path == "/hello") {
                content = "<html><body><h1>Hello World</h1>"
                          "<p><a href='/'>Home</a></p></body></html>";
            }
            else {
                // 404 Not Found
                content = "<html><body><h1>404 Not Found</h1></body></html>";
                socket->write("HTTP/1.1 404 Not Found\r\n"
                              "Content-Type: text/html\r\n"
                              "Content-Length: " + QByteArray::number(content.size()) + "\r\n"
                              "Connection: close\r\n\r\n" + content.toUtf8());
                socket->disconnectFromHost();
                return;
            }
            
            // Send response
            socket->write("HTTP/1.1 200 OK\r\n"
                          "Content-Type: text/html\r\n"
                          "Content-Length: " + QByteArray::number(content.size()) + "\r\n"
                          "Connection: close\r\n\r\n" + content.toUtf8());
                          
            socket->disconnectFromHost();
        });
        
        // Clean up on disconnect
        QObject::connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    });
    
    return app.exec();
}