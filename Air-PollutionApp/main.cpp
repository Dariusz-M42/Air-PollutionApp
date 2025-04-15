#include "mainwindow.h"

class WeatherApp : public QMainWindow {
    Q_OBJECT

public:
    WeatherApp(QWidget *parent = nullptr) : QMainWindow(parent),
        networkManager(new QNetworkAccessManager(this)),
        chartView(new QChartView(this)) {

        setupUI();
        connect(networkManager, &QNetworkAccessManager::finished, this, &WeatherApp::handleNetworkReply);
    }

private slots:
    //funkcja tworząca zapytanie do OpenWeather API
    void fetchAirQualityData() {
        QString address = addressInput->text().trimmed();
        if (address.isEmpty()) {
            QMessageBox::warning(this, "Błąd", "Wprowadź adres (np. 'Warszawa, PL')");  //błąd przy braku wprowadzenia adresu
            return;
        }

        QString geocodeUrl = QString("https://geocoding-api.open-meteo.com/v1/search?name=%1&count=1").arg(address);
        networkManager->get(QNetworkRequest(QUrl(geocodeUrl)));
    }

    //funkcja pobierająca dane
    void handleNetworkReply(QNetworkReply *reply) {
        if (reply->error() != QNetworkReply::NoError) {
            QMessageBox::critical(this, "Błąd sieci", reply->errorString()); //błąd przy braku połączenia z internetem
            reply->deleteLater();
            return;
        }

        QByteArray data = reply->readAll();
        reply->deleteLater();

        try {
            json response = json::parse(data.toStdString());
            //dane w formacie JSON
            if (reply->url().toString().contains("geocoding-api")) {
                if (response.contains("results") && !response["results"].empty()) {
                    double lat = response["results"][0]["latitude"];
                    double lon = response["results"][0]["longitude"];
                    currentLocation = QString::fromStdString(response["results"][0]["name"]);
                    currentCountry = QString::fromStdString(response["results"][0]["country"]);

                    QString airQualityUrl = QString(
                                                "https://air-quality-api.open-meteo.com/v1/air-quality?"
                                                "latitude=%1&longitude=%2&"
                                                "hourly=pm10,pm2_5,nitrogen_dioxide&"
                                                "forecast_days=3"
                                                ).arg(lat).arg(lon);

                    networkManager->get(QNetworkRequest(QUrl(airQualityUrl)));
                }
            }
            else if (reply->url().toString().contains("air-quality")) {
                displayAirQualityData(response);
                saveToJsonFile(response, "air_quality_data.json");
            }
        } catch (const std::exception &e) {
            QMessageBox::critical(this, "Błąd", QString("Błąd przetwarzania danych: %1").arg(e.what()));
        }
    }

    //funkcja pobiera dane zapisane w pliku JSON (dane historyczne z poprzedniego pobrania)
    void loadFromFile() {
        QString fileName = QFileDialog::getOpenFileName(this, "Otwórz plik JSON", "", "JSON Files (*.json)");
        if (fileName.isEmpty()) return;

        try {
            std::ifstream file(fileName.toStdString());
            json data = json::parse(file);

            if (data.contains("location") && data.contains("station") && data.contains("air_quality_data")) {
                currentLocation = QString::fromStdString(data["location"]);
                currentCountry = QString::fromStdString(data["station"]);
                displayAirQualityData(data["air_quality_data"]);
            } else {
                QMessageBox::warning(this, "Błąd", "Nieprawidłowy format pliku JSON");
            }
        } catch (const std::exception &e) {
            QMessageBox::critical(this, "Błąd", QString("Błąd wczytywania pliku: %1").arg(e.what()));
        }
    }

    //funkcja wyświetlająca dane i wykresy
    void displayAirQualityData(const json &data) {
        weatherDisplay->clear();
        clearCharts();

        if (data.contains("hourly")) {
            auto hourly = data["hourly"];
            std::vector<std::string> timeData = hourly["time"].get<std::vector<std::string>>();
            std::vector<double> pm10 = hourly["pm10"].is_null() ? std::vector<double>() : hourly["pm10"].get<std::vector<double>>();
            std::vector<double> pm2_5 = hourly["pm2_5"].is_null() ? std::vector<double>() : hourly["pm2_5"].get<std::vector<double>>();
            std::vector<double> no2 = hourly["nitrogen_dioxide"].is_null() ? std::vector<double>() : hourly["nitrogen_dioxide"].get<std::vector<double>>();

            // Wyświetlanie informacji o stacji
            weatherDisplay->append("📍 Lokalizacja: " + currentLocation);
            weatherDisplay->append("🏭 Stacja pomiarowa: " + currentCountry);

            // Tworzenie wykresów
            createChart(timeData, pm10, "PM10 [µg/m³]", Qt::red);
            createChart(timeData, pm2_5, "PM2.5 [µg/m³]", Qt::blue);
            createChart(timeData, no2, "NO₂ [µg/m³]", Qt::darkGreen);
        }
    }

    //funkcja tworząca wykres
    void createChart(const std::vector<std::string>& timeData, const std::vector<double>& values, const QString& title, const QColor& color) {
        if (values.empty()) return;

        QLineSeries *series = new QLineSeries();
        series->setName(title);

        //konwersja danych czasowych i wartości
        for (size_t i = 0; i < values.size(); ++i) {
            QDateTime dt = QDateTime::fromString(QString::fromStdString(timeData[i]), Qt::ISODate);
            series->append(dt.toMSecsSinceEpoch(), values[i]);
        }

        QChart *chart = new QChart();
        chart->addSeries(series);
        chart->setTitle(title + " - " + currentLocation);
        chart->legend()->setVisible(true);
        chart->setAnimationOptions(QChart::SeriesAnimations);

        //osie czasu
        QDateTimeAxis *axisX = new QDateTimeAxis();
        axisX->setFormat("dd MMM hh:mm");
        axisX->setTitleText("Czas");
        chart->addAxis(axisX, Qt::AlignBottom);
        series->attachAxis(axisX);

        //osie wartości
        QValueAxis *axisY = new QValueAxis();
        axisY->setTitleText(title);
        chart->addAxis(axisY, Qt::AlignLeft);
        series->attachAxis(axisY);

        //ustawienie koloru serii
        QPen pen(color);
        pen.setWidth(2);
        series->setPen(pen);

        //dodanie wykresu do layoutu
        QChartView *chartView = new QChartView(chart);
        chartView->setRenderHint(QPainter::Antialiasing);
        chartsLayout->addWidget(chartView);
        charts.append(chartView);
    }

    //funkcja do usuwania danych z wykresu (potrzebna do utworzenia nowego wykresu dla nowego zapytania)
    void clearCharts() {
        for (auto chart : charts) {
            chartsLayout->removeWidget(chart);
            delete chart;
        }
        charts.clear();
    }

    //funkcja zapisująca pobrane dane do pliku JSON
    void saveToJsonFile(const json &data, const std::string &filename) {
        json output;
        output["location"] = currentLocation.toStdString();
        output["station"] = currentCountry.toStdString();
        output["air_quality_data"] = data;

        std::ofstream file(filename);
        if (file.is_open()) {
            file << output.dump(2);
            QMessageBox::information(this, "Sukces", "Dane zapisane do " + QString::fromStdString(filename));
        } else {
            QMessageBox::warning(this, "Błąd", "Nie można zapisać pliku.");
        }
    }

private:
    QNetworkAccessManager *networkManager;
    QLineEdit *addressInput;
    QTextEdit *weatherDisplay;
    QVBoxLayout *chartsLayout;
    QList<QChartView*> charts;
    QChartView *chartView;
    QString currentLocation;
    QString currentCountry;

    //główne okno aplikacji
    void setupUI() {
        QWidget *centralWidget = new QWidget(this);
        QVBoxLayout *mainLayout = new QVBoxLayout(centralWidget);

        // Wprowadzanie adresu
        QHBoxLayout *inputLayout = new QHBoxLayout();
        inputLayout->addWidget(new QLabel("Adres:"));
        addressInput = new QLineEdit();
        addressInput->setPlaceholderText("np. 'Kraków, PL'");
        inputLayout->addWidget(addressInput);
        mainLayout->addLayout(inputLayout);

        // Przyciski
        QHBoxLayout *buttonLayout = new QHBoxLayout();
        QPushButton *fetchButton = new QPushButton("Pobierz dane");
        connect(fetchButton, &QPushButton::clicked, this, &WeatherApp::fetchAirQualityData);
        buttonLayout->addWidget(fetchButton);

        QPushButton *loadButton = new QPushButton("Wczytaj z pliku");
        connect(loadButton, &QPushButton::clicked, this, &WeatherApp::loadFromFile);
        buttonLayout->addWidget(loadButton);
        mainLayout->addLayout(buttonLayout);

        //wyświetlanie danych
        weatherDisplay = new QTextEdit();
        weatherDisplay->setReadOnly(true);
        weatherDisplay->setMaximumHeight(150);
        mainLayout->addWidget(weatherDisplay);

        //wykresy
        QScrollArea *scrollArea = new QScrollArea();
        QWidget *chartsWidget = new QWidget();
        chartsLayout = new QVBoxLayout(chartsWidget);
        scrollArea->setWidget(chartsWidget);
        scrollArea->setWidgetResizable(true);
        mainLayout->addWidget(scrollArea);

        setCentralWidget(centralWidget);
        setWindowTitle("Air-PollutionApp");
        resize(1000, 800);
    }
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    WeatherApp window;
    window.show();
    return app.exec();
}

#include "main.moc"
