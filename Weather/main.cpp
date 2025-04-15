#include "mainwindow.h"

class WeatherApp : public QMainWindow {
    Q_OBJECT

public:
    WeatherApp(QWidget *parent = nullptr) : QMainWindow(parent), networkManager(new QNetworkAccessManager(this)) {
        setupUI();
        connect(networkManager, &QNetworkAccessManager::finished, this, &WeatherApp::handleNetworkReply);
    }

private slots:
    void fetchWeatherData() {
        QString address = addressInput->text().trimmed();
        if (address.isEmpty()) {
            QMessageBox::warning(this, "Błąd", "Wprowadź adres (np. 'Warszawa, PL')");
            return;
        }

        // 1. Geokodowanie: Zamiana adresu na współrzędne (latitude, longitude)
        QString geocodeUrl = QString("https://geocoding-api.open-meteo.com/v1/search?name=%1&count=1").arg(address);
        networkManager->get(QNetworkRequest(QUrl(geocodeUrl)));
    }

    void handleNetworkReply(QNetworkReply *reply) {
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::critical(this, "Błąd sieci", reply->errorString());
            reply->deleteLater();
            return;
        }

        QByteArray data = reply->readAll();
        reply->deleteLater();

        try {
            json response = json::parse(data.toStdString());

            if (reply->url().toString().contains("geocoding-api")) {
                // Odpowiedź z geokodowania - pobierz współrzędne
                if (response.contains("results") && !response["results"].empty()) {
                    double lat = response["results"][0]["latitude"];
                    double lon = response["results"][0]["longitude"];
                    currentLocation = QString::fromStdString(response["results"][0]["name"]);

                    // 2. Pobierz dane pogodowe dla znalezionych współrzędnych
                    QString weatherUrl = QString("https://api.open-meteo.com/v1/forecast?latitude=%1&longitude=%2&hourly=temperature_2m")
                                             .arg(lat).arg(lon);
                    networkManager->get(QNetworkRequest(QUrl(weatherUrl)));

                    weatherDisplay->append("Znaleziono lokalizację: " + currentLocation);
                } else {
                    QMessageBox::warning(this, "Błąd", "Nie znaleziono podanego adresu.");
                }
            }
            else if (reply->url().toString().contains("forecast")) {
                // Odpowiedź z danymi pogodowymi
                displayWeatherData(response);
                saveToJsonFile(response, "weather_data.json");
            }
        } catch (const std::exception &e) {
            QMessageBox::critical(this, "Błąd parsowania JSON", e.what());
        }
    }

    void loadFromFile() {
        QString fileName = QFileDialog::getOpenFileName(this, "Otwórz plik JSON", "", "JSON Files (*.json)");
        if (fileName.isEmpty()) return;

        try {
            std::ifstream file(fileName.toStdString());
            json data = json::parse(file);
            displayWeatherData(data);
        } catch (const std::exception &e) {
            QMessageBox::critical(this, "Błąd", QString("Błąd wczytywania pliku: %1").arg(e.what()));
        }
    }

private:
    QNetworkAccessManager *networkManager;
    QLineEdit *addressInput;
    QTextEdit *weatherDisplay;
    QChartView *chartView;
    QString currentLocation;

    void setupUI() {
        QWidget *centralWidget = new QWidget(this);
        QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

        // Wprowadzanie adresu
        QHBoxLayout *inputLayout = new QHBoxLayout();
        inputLayout->addWidget(new QLabel("Adres (np. 'Kraków, PL'):"));
        addressInput = new QLineEdit();
        inputLayout->addWidget(addressInput);
        mainLayout->addLayout(inputLayout);

        // Przyciski
        QHBoxLayout *buttonLayout = new QHBoxLayout();
        QPushButton *fetchButton = new QPushButton("Pobierz dane");
        connect(fetchButton, &QPushButton::clicked, this, &WeatherApp::fetchWeatherData);
        buttonLayout->addWidget(fetchButton);

        QPushButton *loadButton = new QPushButton("Wczytaj z pliku");
        connect(loadButton, &QPushButton::clicked, this, &WeatherApp::loadFromFile);
        buttonLayout->addWidget(loadButton);
        mainLayout->addLayout(buttonLayout);

        // Wyświetlanie danych tekstowych
        weatherDisplay = new QTextEdit();
        weatherDisplay->setReadOnly(true);
        mainLayout->addWidget(weatherDisplay);

        // Wykres temperatury
        chartView = new QChartView();
        chartView->setRenderHint(QPainter::Antialiasing);
        mainLayout->addWidget(chartView);

        setCentralWidget(centralWidget);
        setWindowTitle("Aplikacja Pogodowa (OpenMeteo)");
        resize(800, 600);
    }

    void displayWeatherData(const json &data) {
        weatherDisplay->clear();

        if (data.contains("hourly")) {
            auto hourly = data["hourly"];
            std::vector<double> temperatures = hourly["temperature_2m"];
            std::vector<std::string> times = hourly["time"];

            // Wyświetlanie danych tekstowych
            weatherDisplay->append("Lokalizacja: " + currentLocation);
            weatherDisplay->append("Ostatnia temperatura: " + QString::number(temperatures.back()) + "°C");

            // Generowanie wykresu
            QLineSeries *series = new QLineSeries();
            for (size_t i = 0; i < temperatures.size(); ++i) {
                series->append(i, temperatures[i]);
            }

            QChart *chart = new QChart;
            chart->addSeries(series);
            chart->setTitle("Temperatura w czasie (" + currentLocation + ")");

            QValueAxis *axisX = new QValueAxis();
            axisX->setTitleText("Godziny");
            chart->addAxis(axisX, Qt::AlignBottom);
            series->attachAxis(axisX);

            QValueAxis *axisY = new QValueAxis();
            axisY->setTitleText("Temperatura (°C)");
            chart->addAxis(axisY, Qt::AlignLeft);
            series->attachAxis(axisY);

            chartView->setChart(chart);
        } else {
            weatherDisplay->setText("Brak danych pogodowych w odpowiedzi.");
        }
    }

    void saveToJsonFile(const json &data, const std::string &filename) {
        std::ofstream file(filename);
        if (!file.is_open()) {
            QMessageBox::warning(this, "Błąd", "Nie można zapisać pliku.");
            return;
        }
        file << data.dump(2);
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    WeatherApp window;
    window.show();

    return app.exec();
}

#include "main.moc"
