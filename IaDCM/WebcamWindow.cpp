#include "WebcamWindow.h"
#include <QMediaDevices>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QApplication>
#include <QTimer>

WebcamWindow::WebcamWindow(QWidget* parent)
    : QWidget(parent)
{
    // Устанавливаем заголовок окна и его начальный размер
    setWindowTitle("Веб-камера");
    resize(800, 600);

    initUI();
    initCamera();
}

WebcamWindow::~WebcamWindow()
{
    // Гарантированно освобождаем горячие клавиши при закрытии окна
    if (m_isStealthMode) {
        UnregisterHotKey((HWND)this->winId(), TAKE_PHOTO_HOTKEY_ID);
        UnregisterHotKey((HWND)this->winId(), EXIT_STEALTH_HOTKEY_ID);
    }

    if (m_camera) {
        m_camera->stop();
    }
    delete m_captureSession;
    delete m_imageCapture;
    delete m_mediaRecorder;
    delete m_camera;
}

void WebcamWindow::initUI()
{
    // --- 1. Определение констант для размеров ---
    // Это делает код чище и позволяет легко менять размеры в одном месте.
    const int VIDEO_WIDTH = 800; // Базовая ширина для видео (16)
    const int VIDEO_HEIGHT = VIDEO_WIDTH * 9 / 16; // Рассчитываем высоту для соотношения 16:9 (9)

    const int BUTTON_HEIGHT = 50; // Базовая высота для кнопок (1)
    const int BUTTON_WIDTH = BUTTON_HEIGHT * 4; // Рассчитываем ширину для соотношения 2:1 (2)

    // --- 2. Создание виджетов (как и раньше) ---
    m_mainLayout = new QVBoxLayout(); // <-- Убрали 'this' из конструктора, установим лэйаут в конце
    m_videoWidget = new QVideoWidget(this);
    m_cameraSelection = new QComboBox(this);
    m_photoButton = new QPushButton("Сделать фото", this);
    m_videoButton = new QPushButton("Начать запись", this);
    m_stealthModeButton = new QPushButton("Скрытый режим", this);
    m_statusLabel = new QLabel("Готово", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);

    // --- 3. Установка фиксированных размеров для виджетов ---
    m_videoWidget->setFixedSize(VIDEO_WIDTH, VIDEO_HEIGHT); // <-- Фиксируем размер видео


    // --- 4. Добавление виджетов в лэйаут с выравниванием ---
    // Мы добавляем кнопки с флагом Qt::AlignHCenter, чтобы они красиво стояли по центру.
    m_mainLayout->addWidget(m_videoWidget);
    m_mainLayout->addWidget(m_cameraSelection);
    m_mainLayout->addWidget(m_photoButton);
    m_mainLayout->addWidget(m_videoButton);
    m_mainLayout->addWidget(m_stealthModeButton);
    m_mainLayout->addWidget(m_statusLabel);

    m_mainLayout->setSpacing(10); // Немного уменьшим расстояние между элементами
    m_mainLayout->setContentsMargins(15, 15, 15, 15);

    // --- 5. Подключение сигналов (как и раньше) ---
    connect(m_cameraSelection, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &WebcamWindow::setupCamera);
    connect(m_photoButton, &QPushButton::clicked, this, &WebcamWindow::capturePhoto);
    connect(m_videoButton, &QPushButton::clicked, this, &WebcamWindow::toggleVideoRecording);
    connect(m_stealthModeButton, &QPushButton::clicked, this, &WebcamWindow::toggleStealthMode);

    // --- ПРИМЕНЕНИЕ СТИЛЕЙ ---
    // Используем C++11 Raw String Literals R"(...)" для удобства
    QString qss = R"(
        QWidget {
            background-color: #1a202c;
            color: #e2e8f0;
            font-family: "Segoe UI", Arial, sans-serif;
        }

        QPushButton {
            background-image: url(:/HubWindow/Button1.png); /* <-- ЗАМЕНИТЕ НА ВАШ ПУТЬ */

            background-repeat: no-repeat;

            background-position: center;

            background-color: transparent;
            border: none;

            /* 5. Стили для текста (остаются как были) */
            color: #90cdf4;
            font-size: 14px;
            font-weight: bold;
            padding: 15px;
            padding-left: 50px;
            padding-right: 50px;
        }

        QPushButton:hover {
            /* Просто меняем картинку для состояния наведения */
            background-image: url(:/HubWindow/Button1_hover.png); /* <-- ЗАМЕНИТЕ НА ВАШ ПУТЬ */
            color: #ffffff;
        }

        QPushButton:pressed {
            /* Меняем картинку для состояния нажатия */
            background-image: url(:/HubWindow/Button1_pressed.png); /* <-- ЗАМЕНИТЕ НА ВАШ ПУТЬ */
            color: #bee3f8;
            /* Эффект смещения текста можно оставить */
            padding-top: 7px;
            padding-left: 6px;
        }

        QLabel {
            color: #a0aec0;
            font-size: 12px;
        }

        QVideoWidget {
            border: 2px solid #2b6cb0;
            border-radius: 5px;
        }

        QComboBox {
            border: 1px solid #2b6cb0;
            border-radius: 4px;
            padding: 3px 10px 3px 5px;
            background-color: #2d3748;
            color: #e2e8f0;
        }

        QComboBox::down-arrow {
            /* Можно добавить свою иконку стрелки, если есть */
        }

        QComboBox QAbstractItemView {
            border: 2px solid #2b6cb0;
            background-color: #2d3748;
            selection-background-color: #4a5568;
            color: #e2e8f0;
        }
    )";

    this->setStyleSheet(qss);

    // --- 7. Финальная настройка размера окна ---
    this->setLayout(m_mainLayout); // Устанавливаем лэйаут для окна
    this->adjustSize();            // Просим окно "сжаться" до минимально необходимого размера
    this->setFixedSize(this->size()); // <-- ЗАПРЕЩАЕМ ИЗМЕНЯТЬ РАЗМЕР ОКНА
}


void WebcamWindow::initCamera()
{
    // Получаем список доступных видеоустройств (камер)
    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (cameras.isEmpty()) {
        QMessageBox::critical(this, "Ошибка", "Веб-камеры не найдены!");
        return;
    }

    // Заполняем ComboBox названиями камер
    for (const QCameraDevice& cameraDevice : cameras) {
        m_cameraSelection->addItem(cameraDevice.description());
    }

    // Инициализируем камеру по умолчанию
    setupCamera(0);
}

void WebcamWindow::setupCamera(int index)
{
    // --- 1. Очистка предыдущих объектов ---
    // Важно правильно освободить ресурсы перед созданием новых
    if (m_camera) {
        m_camera->stop();
    }
    delete m_captureSession;
    delete m_imageCapture;
    delete m_mediaRecorder;
    delete m_camera;

    // Обнуляем указатели, чтобы избежать их повторного использования
    m_captureSession = nullptr;
    m_imageCapture = nullptr;
    m_mediaRecorder = nullptr;
    m_camera = nullptr;

    const QList<QCameraDevice> cameras = QMediaDevices::videoInputs();
    if (index >= cameras.size() || cameras.isEmpty()) {
        m_statusLabel->setText("Камеры не найдены или выбрана неверно.");
        return;
    }

    // --- 2. Создание новых экземпляров ---
    m_camera = new QCamera(cameras[index]);

    // Создаем компоненты с помощью конструкторов по умолчанию
    m_imageCapture = new QImageCapture;
    m_mediaRecorder = new QMediaRecorder;

    m_captureSession = new QMediaCaptureSession;

    // --- 3. Связывание компонентов через сессию ---
    m_captureSession->setCamera(m_camera);
    m_captureSession->setImageCapture(m_imageCapture);
    m_captureSession->setRecorder(m_mediaRecorder);
    m_captureSession->setVideoOutput(m_videoWidget);

    // --- 4. Подключение сигналов ---
    // Обязательно проверяем, что объект был создан, перед подключением

    // (Опционально, но рекомендуется) Добавим обработку ошибок
    connect(m_camera, &QCamera::errorOccurred, this, [this]() {
        m_statusLabel->setText("Ошибка камеры: " + m_camera->errorString());
        });

    // --- 5. Запуск камеры ---
    m_camera->start();
    m_statusLabel->setText("Камера \"" + cameras[index].description() + "\" активна.");
}
// WebcamWindow.cpp

void WebcamWindow::capturePhoto()
{
    // Проверяем, готова ли камера к захвату
    if (!m_imageCapture || !m_imageCapture->isReadyForCapture()) {
        m_statusLabel->setText("Ошибка: Камера не готова для фото.");
        return;
    }

    // Создаем уникальное имя файла
    QString fileName = "photo_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".jpg";
    QString savePath = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    savePath += "/Qt";

    QDir dir(savePath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }

    QString fullPath = dir.filePath(fileName);


    m_imageCapture->captureToFile(fullPath);

    m_statusLabel->setText("Фото сохранено в: " + fullPath);
}


void WebcamWindow::toggleVideoRecording()
{
    if (m_isRecording) {
        // --- Останавливаем запись ---
        m_mediaRecorder->stop();
        m_videoButton->setText("Начать запись видео");
        m_statusLabel->setText("Видео сохранено.");
        m_isRecording = false;
    }
    else {
        // --- Начинаем запись ---
        QString fileName = "video_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".mp4";
        QString savePath = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        savePath += "/Qt";

        QDir dir(savePath);
        if (!dir.exists()) {
            dir.mkpath(".");
        }

        m_mediaRecorder->setOutputLocation(QUrl::fromLocalFile(dir.filePath(fileName)));
        m_mediaRecorder->record();

        m_videoButton->setText("Остановить запись");
        m_statusLabel->setText("Идет запись видео...");
        m_isRecording = true;
    }
}

void WebcamWindow::toggleStealthMode()
{
    if (!m_isStealthMode)
    {
        // --- ВХОДИМ В СКРЫТЫЙ РЕЖИМ ---

        // Регистрируем горячие клавиши
        // Ctrl+Alt+P - сделать фото
        // Ctrl+Alt+X - выйти из скрытого режима
        bool photoHotKey = RegisterHotKey((HWND)this->winId(), TAKE_PHOTO_HOTKEY_ID, MOD_CONTROL | MOD_ALT, 'P');
        bool exitHotKey = RegisterHotKey((HWND)this->winId(), EXIT_STEALTH_HOTKEY_ID, MOD_CONTROL | MOD_ALT, 'X');

        if (!photoHotKey || !exitHotKey) {
            m_statusLabel->setText("Ошибка: Не удалось зарегистрировать горячие клавиши.");
            // Если что-то пошло не так, отменяем регистрацию
            UnregisterHotKey((HWND)this->winId(), TAKE_PHOTO_HOTKEY_ID);
            UnregisterHotKey((HWND)this->winId(), EXIT_STEALTH_HOTKEY_ID);
            return;
        }

        m_isStealthMode = true;
        // Даем пользователю инструкцию перед тем, как окно скроется
        m_statusLabel->setText("Скрытый режим активен. Ctrl+Alt+P = фото, Ctrl+Alt+X = выход.");
        QMessageBox::information(this, "Скрытый режим",
            "Скрытый режим активирован.\n\n"
            "Нажмите Ctrl+Alt+P, чтобы сделать скрытый снимок.\n"
            "Нажмите Ctrl+Alt+X, чтобы вернуться в обычный режим.");

        this->hide(); // Скрываем окно с экрана и панели задач
    }
    else
    {
        // --- ВЫХОДИМ ИЗ СКРЫТОГО РЕЖИМА ---
        UnregisterHotKey((HWND)this->winId(), TAKE_PHOTO_HOTKEY_ID);
        UnregisterHotKey((HWND)this->winId(), EXIT_STEALTH_HOTKEY_ID);

        m_isStealthMode = false;
        m_statusLabel->setText("Готово");
        this->show(); // Показываем окно обратно
    }
}

bool WebcamWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result)
{
    // Проверяем, что это нативное событие Windows
    if (eventType == "windows_generic_MSG")
    {
        MSG* msg = static_cast<MSG*>(message);

        // Если это сообщение о нажатии горячей клавиши
        if (msg->message == WM_HOTKEY)
        {
            // Определяем, какая именно клавиша была нажата по ее ID
            if (msg->wParam == TAKE_PHOTO_HOTKEY_ID)
            {
                capturePhoto(); // Вызываем наш метод для создания фото
                *result = 1;
                return true; // Сообщаем, что мы обработали событие
            }
            else if (msg->wParam == EXIT_STEALTH_HOTKEY_ID)
            {
                toggleStealthMode(); // Выходим из скрытого режима
                *result = 1;
                return true;
            }
        }
    }
    // Передаем все остальные события для стандартной обработки
    return QWidget::nativeEvent(eventType, message, result);
}

