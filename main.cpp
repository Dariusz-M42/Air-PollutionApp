#include "mainwindow.h"

/*!
 * \brief Klasa obsługująca pełne działanie aplikacji
 * \details W tej klasie znajdują się wszystkie funkcje zapewniające działanie aplikacji.
 * Aplikacja pobiera dane z OpenWeather API, wyświetla dane o jakości powietrza w formie wykresów,
 * podaje lokalizację oraz kraj stacji pomiarowej, podaje wartości minimalne, maksymalne oraz średnie
 * dla szkodliwych czynników typu PM10, PM2.5 oraz NO2. Aplikacja oferuje "tryb offline": jeśli wcześniej
 * aplikacja została wykorzystana do wyszukania danych dla dowolnej lokalizacji, będzie można skorzystać z zapisanego
 * pliku JSON do odtworzenia poprzednich danych.
 * \author Dariusz Murawko
 * \version 1.4
 * \date 22/04/2025
 * \bug Skalowanie wykresów uniemożliwia odczytanie osi czasu - użytkownik powinien przyjąć,
 * że każdy fragment wykresu oddzielony pionowo oznacza jeden dzień przy czym początek wykresu
 * to dane sprzed dwóch dni a koniec to prognoza na trzeci dzień od dnia wywołania skryptu.
 * \copyright GNU Public License
 */

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
    // Funkcja tworząca zapytanie do OpenWeather API
    void fetchAirQualityData() {
        QString address = addressInput->text().trimmed();
        if (address.isEmpty()) {
            QMessageBox::warning(this, "Błąd", "Wprowadź adres (np. 'Warszawa, PL')");
            return;
        }

        QString geocodeUrl = QString("https://geocoding-api.open-meteo.com/v1/search?name=%1&count=1").arg(address);
        networkManager->get(QNetworkRequest(QUrl(geocodeUrl)));
    }

    // Funkcja pobierająca dane
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
            // Dane w formacie JSON
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
                                                "past_days=2&"
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

    // Funkcja pobierająca dane zapisane w pliku JSON (dane historyczne z poprzedniego pobrania)
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

    // Funkcja wyświetlająca dane i wykresy
    void displayAirQualityData(const json &data) {
        weatherDisplay->clear();
        clearCharts();
        statsDisplay->clear();

        if (data.contains("hourly")) {
            auto hourly = data["hourly"];
            std::vector<std::string> timeData = hourly["time"].get<std::vector<std::string>>();

            // Pobierz dane i oblicz statystyki
            processParameter(hourly, "pm10", "PM10 [µg/m³]", Qt::red);
            processParameter(hourly, "pm2_5", "PM2.5 [µg/m³]", Qt::blue);
            processParameter(hourly, "nitrogen_dioxide", "NO₂ [µg/m³]", Qt::darkGreen);

            // Wyświetlanie informacji o stacji
            weatherDisplay->append("Lokalizacja: "+ currentLocation);
            weatherDisplay->append("Stacja pomiarowa: " + currentCountry);
        }
    }

    // Funkcja obliczająca, zapisująca i wyświetlająca statystyki (minimum, maksimum, średnia)
    void processParameter(const json& hourly, const std::string& param, const QString& title, const QColor& color) {
        if (!hourly.contains(param) || hourly[param].is_null()) return;

        std::vector<double> values = hourly[param].get<std::vector<double>>();
        std::vector<std::string> timeData = hourly["time"].get<std::vector<std::string>>();

        auto [min_it, max_it] = std::minmax_element(values.begin(), values.end());
        double min_val = *min_it;
        double max_val = *max_it;
        double avg_val = std::accumulate(values.begin(), values.end(), 0.0) / values.size();

        parameterStats[QString::fromStdString(param)] = {min_val, max_val, avg_val};

        statsDisplay->append(QString("%1\n  Min: %2\n  Max: %3\n  Średnia: %4\n")
                                 .arg(title)
                                 .arg(min_val, 0, 'f', 1)
                                 .arg(max_val, 0, 'f', 1)
                                 .arg(avg_val, 0, 'f', 1));

        createChart(timeData, values, title, color);
    }

    // Funkcja tworząca wykresy
    void createChart(const std::vector<std::string>& timeData, const std::vector<double>& values, const QString& title, const QColor& color) {
        QLineSeries *series = new QLineSeries();
        series->setName(title);

        for (size_t i = 0; i < values.size(); ++i) {
            QDateTime dt = QDateTime::fromString(QString::fromStdString(timeData[i]), Qt::ISODate);
            series->append(dt.toMSecsSinceEpoch(), values[i]);
        }

        QChart *chart = new QChart();
        chart->addSeries(series);
        chart->setTitle(title + " - " + currentLocation);
        chart->legend()->setVisible(true);
        chart->setAnimationOptions(QChart::SeriesAnimations);

        QDateTimeAxis *axisX = new QDateTimeAxis();
        axisX->setFormat("dd MM hh:mm");
        axisX->setTitleText("Czas");
        chart->addAxis(axisX, Qt::AlignBottom);
        series->attachAxis(axisX);

        QValueAxis *axisY = new QValueAxis();
        axisY->setTitleText(title);
        chart->addAxis(axisY, Qt::AlignLeft);
        series->attachAxis(axisY);

        QPen pen(color);
        pen.setWidth(2);
        series->setPen(pen);

        QChartView *chartView = new QChartView(chart);
        chartView->setRenderHint(QPainter::Antialiasing);
        chartsLayout->addWidget(chartView);
        charts.append(chartView);
    }

    // Funkcja do czyszczenia okien wykresów
    void clearCharts() {
        for (auto chart : charts) {
            chartsLayout->removeWidget(chart);
            delete chart;
        }
        charts.clear();
    }

    // Funkcja obsługująca zapis danych do pliku JSON
    void saveToJsonFile(const json &data, const std::string &filename) {
        json output;
        output["location"] = currentLocation.toStdString();
        output["station"] = currentCountry.toStdString();
        output["air_quality_data"] = data;

        // Dodaj statystyki do pliku JSON
        json stats;
        for (const auto& item : parameterStats.toStdMap()) {
            const auto& param = item.first;
            const auto& statsData = item.second;
            stats[param.toStdString()] = {
                {"min", statsData.min},
                {"max", statsData.max},
                {"avg", statsData.avg}
            };
        }
        output["statistics"] = stats;

        std::ofstream file(filename);
        if (file.is_open()) {
            file << output.dump(2);
            QMessageBox::information(this, "Sukces", "Dane zapisane do " + QString::fromStdString(filename));
        } else {
            QMessageBox::warning(this, "Błąd", "Nie można zapisać pliku.");
        }
    }

private:
    struct ParameterStats {
        double min;
        double max;
        double avg;
    };

    QNetworkAccessManager *networkManager;
    QLineEdit *addressInput;
    QTextEdit *weatherDisplay;
    QTextEdit *statsDisplay;
    QVBoxLayout *chartsLayout;
    QList<QChartView*> charts;
    QChartView *chartView;
    QString currentLocation;
    QString currentCountry;
    QMap<QString, ParameterStats> parameterStats;

    // Funkcja tworząca główne okno aplikacji
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

        // Wyświetlanie danych
        weatherDisplay = new QTextEdit();
        weatherDisplay->setReadOnly(true);
        weatherDisplay->setMaximumHeight(100);
        mainLayout->addWidget(weatherDisplay);

        // Statystyki
        statsDisplay = new QTextEdit();
        statsDisplay->setReadOnly(true);
        statsDisplay->setMaximumHeight(150);
        mainLayout->addWidget(statsDisplay);

        // Wykresy
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
