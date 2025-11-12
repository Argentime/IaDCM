#include "WebcamWindow.h"
#include <QMediaDevices>
#include <QMessageBox>
#include <QStandardPaths>
#include <QDateTime>
#include <QDir>
#include <QApplication>
#include <QTimer>

QString GUIDToQString(const GUID& guid)
{
    if (guid == MFVideoFormat_MJPG) return "MJPG";
    if (guid == MFVideoFormat_YUY2) return "YUY2";
    if (guid == MFVideoFormat_NV12) return "NV12";
    if (guid == MFVideoFormat_RGB24) return "RGB24";
    if (guid == MFVideoFormat_RGB32) return "RGB32";

    OLECHAR guidString[39];
    StringFromGUID2(guid, guidString, 39);
    return QString::fromWCharArray(guidString);
}

WebcamWindow::WebcamWindow(QWidget* parent)
    : QWidget(parent)
{
    // Устанавливаем заголовок окна и его начальный размер
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    
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
    CoUninitialize();
}

void WebcamWindow::initUI()
{
    // --- 1. Определение констант для размеров (без изменений) ---
    const int VIDEO_WIDTH = 800;
    const int VIDEO_HEIGHT = VIDEO_WIDTH * 9 / 16;
    const int BUTTON_HEIGHT = 50;
    const int BUTTON_WIDTH = BUTTON_HEIGHT * 2;

    // --- 2. Создание виджетов (без изменений) ---
    m_mainLayout = new QVBoxLayout();
    m_videoWidget = new QVideoWidget(this);
    m_infoTextEdit = new QTextEdit(this);
    m_infoTextEdit->setReadOnly(true);
    m_cameraSelection = new QComboBox(this);
    m_photoButton = new QPushButton("Сделать фото", this);
    m_videoButton = new QPushButton("Начать запись", this);
    m_stealthModeButton = new QPushButton("Скрытый режим", this);
    m_statusLabel = new QLabel("Готово", this);
    m_statusLabel->setAlignment(Qt::AlignCenter);

    // --- 3. Настройка размеров виджетов (без изменений) ---
    m_videoWidget->setFixedSize(VIDEO_WIDTH, VIDEO_HEIGHT);


    // --- 4. НОВАЯ ЛОГИКА КОМПОНОВКИ ---

    // Создаем горизонтальный лэйаут для нижней части (информация + кнопки)
    QHBoxLayout* bottomControlsLayout = new QHBoxLayout();

    // Создаем вертикальный лэйаут только для кнопок
    QVBoxLayout* buttonsLayout = new QVBoxLayout();

    // Добавляем кнопки в их собственный лэйаут
    buttonsLayout->addWidget(m_photoButton);
    buttonsLayout->addWidget(m_videoButton);
    buttonsLayout->addWidget(m_stealthModeButton);
    buttonsLayout->addStretch(); // Добавляем "растяжку", чтобы кнопки прижались кверху

    // Теперь собираем горизонтальный лэйаут:
    // Слева - текстовое поле. Второй параметр (1) - "коэффициент растяжения".
    bottomControlsLayout->addWidget(m_infoTextEdit, 1);
    // Справа - лэйаут с кнопками. Коэффициент 0 означает, что он займет минимально необходимое место.
    bottomControlsLayout->addLayout(buttonsLayout, 0);

    // Собираем главный вертикальный лэйаут окна
    m_mainLayout->addWidget(m_videoWidget);
    m_mainLayout->addWidget(m_cameraSelection);
    m_mainLayout->addLayout(bottomControlsLayout); // Добавляем всю нашу горизонтальную секцию
    m_mainLayout->addWidget(m_statusLabel);

    m_mainLayout->setSpacing(10);
    m_mainLayout->setContentsMargins(15, 15, 15, 15);

    // --- 5. Подключение сигналов (без изменений) ---
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
            background-image: url(:/HubWindow/Button1_hover.png);
            color: #ffffff;
        }

        QPushButton:pressed {
            /* Меняем картинку для состояния нажатия */
            background-image: url(:/HubWindow/Button1_pressed.png); 
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
        QTextEdit {
            background-color: #2d3748; /* Темный фон */
            border: 1px solid #2b6cb0; /* Синяя рамка */
            border-radius: 4px;
            color: #e2e8f0; /* Светлый текст */
            font-family: "Consolas", "Courier New", monospace; /* Моноширинный шрифт для аккуратного вида */
            padding: 5px;
        }
    )";

    this->setStyleSheet(qss);

    // --- 7. Финальная настройка размера окна (без изменений) ---
    this->setLayout(m_mainLayout);
    this->adjustSize();
    this->setFixedSize(this->size());
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

    updateWebcamDetails(index);
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

void WebcamWindow::updateWebcamDetails(int index)
{
    WebcamInfo info = getWinApiWebcamInfo(index);

    QString htmlContent =
        QString("<b>Имя:</b><pre>%1</pre>"
            "<b>Производитель:</b><pre>%2</pre>"
            "<b>Версия драйвера:</b><pre>%3</pre>"
            "<b>Hardware IDs:</b><pre>%4</pre>"
            "<b>Символьная ссылка:</b><pre>%5</pre>"
            "<b>Поддерживаемые форматы:</b><pre>%6</pre>")
        .arg(info.friendlyName.isEmpty() ? "N/A" : info.friendlyName)
        .arg(info.manufacturer.isEmpty() ? "N/A" : info.manufacturer)
        .arg(info.driverVersion.isEmpty() ? "N/A" : info.driverVersion)
        .arg(info.hardwareIDs.isEmpty() ? "N/A" : info.hardwareIDs)
        .arg(info.supportedFormats.isEmpty() ? "N/A" : info.supportedFormats);

    m_infoTextEdit->setHtml(htmlContent);
}

// Основная функция для работы с Windows Media Foundation
WebcamInfo WebcamWindow::getWinApiWebcamInfo(int index)
{
    WebcamInfo info;
    HRESULT hr;

    // --- ЧАСТЬ 1: Инициализация и получение базовых данных из WMF ---
    hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) return info;

    IMFAttributes* pAttributes = nullptr;
    hr = MFCreateAttributes(&pAttributes, 1);
    if (FAILED(hr)) { MFShutdown(); return info; }

    hr = pAttributes->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (FAILED(hr)) { pAttributes->Release(); MFShutdown(); return info; }

    IMFActivate** ppDevices = nullptr;
    UINT32 count = 0;
    hr = MFEnumDeviceSources(pAttributes, &ppDevices, &count);

    if (SUCCEEDED(hr) && count > (UINT32)index)
    {
        IMFActivate* pDevice = ppDevices[index];

        // Получаем имя и символьную ссылку (как и раньше)
        WCHAR* tempString = nullptr;
        UINT32 length = 0;
        pDevice->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &tempString, &length);
        info.friendlyName = QString::fromWCharArray(tempString);
        CoTaskMemFree(tempString);

        pDevice->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK, &tempString, &length);
        info.symbolicLink = QString::fromWCharArray(tempString);
        CoTaskMemFree(tempString);

        // --- ЧАСТЬ 2: Получение списка поддерживаемых форматов из WMF ---
        IMFMediaSource* pSource = nullptr;
        if (SUCCEEDED(pDevice->ActivateObject(IID_PPV_ARGS(&pSource)))) {
            IMFPresentationDescriptor* pPD = nullptr;
            if (SUCCEEDED(pSource->CreatePresentationDescriptor(&pPD))) {
                DWORD streamCount = 0;
                pPD->GetStreamDescriptorCount(&streamCount);
                if (streamCount > 0) {
                    IMFStreamDescriptor* pSD = nullptr;
                    BOOL selected;
                    pPD->GetStreamDescriptorByIndex(0, &selected, &pSD);

                    IMFMediaTypeHandler* pHandler = nullptr;
                    pSD->GetMediaTypeHandler(&pHandler);
                    DWORD mediaTypeCount = 0;
                    pHandler->GetMediaTypeCount(&mediaTypeCount);

                    QStringList formatsList;
                    for (DWORD i = 0; i < mediaTypeCount; ++i) {
                        IMFMediaType* pMT = nullptr;
                        pHandler->GetMediaTypeByIndex(i, &pMT);

                        UINT32 width, height, fr_num, fr_den;
                        GUID subtype;
                        MFGetAttributeSize(pMT, MF_MT_FRAME_SIZE, &width, &height);
                        MFGetAttributeRatio(pMT, MF_MT_FRAME_RATE, &fr_num, &fr_den);
                        pMT->GetGUID(MF_MT_SUBTYPE, &subtype);

                        double fps = (fr_den == 0) ? 0.0 : (double)fr_num / fr_den;
                        formatsList.append(QString("%1x%2 @ %3 FPS (%4)")
                            .arg(width).arg(height).arg(fps, 0, 'f', 2).arg(GUIDToQString(subtype)));

                        pMT->Release();
                    }
                    info.supportedFormats = formatsList.join("\n");
                    pHandler->Release();
                    pSD->Release();
                }
                pPD->Release();
            }
            pSource->Release();
        }

        // --- ЧАСТЬ 3: Получение информации об устройстве из SetupAPI ---
        HDEVINFO hDevInfo = SetupDiGetClassDevs(&KSCATEGORY_CAPTURE, NULL, NULL, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (hDevInfo != INVALID_HANDLE_VALUE) {
            SP_DEVICE_INTERFACE_DATA devInterfaceData;
            devInterfaceData.cbSize = sizeof(SP_DEVICE_INTERFACE_DATA);
            if (SetupDiOpenDeviceInterface(hDevInfo, info.symbolicLink.toStdWString().c_str(), 0, &devInterfaceData)) {
                SP_DEVINFO_DATA devInfoData;
                devInfoData.cbSize = sizeof(SP_DEVINFO_DATA);
                DWORD requiredSize = 0;
                SetupDiGetDeviceInterfaceDetail(hDevInfo, &devInterfaceData, NULL, 0, &requiredSize, NULL);

                PSP_DEVICE_INTERFACE_DETAIL_DATA devInterfaceDetailData = (PSP_DEVICE_INTERFACE_DETAIL_DATA) new BYTE[requiredSize];
                devInterfaceDetailData->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA);

                if (SetupDiGetDeviceInterfaceDetail(hDevInfo, &devInterfaceData, devInterfaceDetailData, requiredSize, &requiredSize, &devInfoData)) {
                    TCHAR buffer[256];
                    // Производитель
                    if (SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfoData, SPDRP_MFG, NULL, (PBYTE)buffer, sizeof(buffer), NULL))
                        info.manufacturer = QString::fromWCharArray(buffer);
                    // Версия драйвера
                    if (SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfoData, SPDRP_DRIVER, NULL, (PBYTE)buffer, sizeof(buffer), NULL))
                        info.driverVersion = QString::fromWCharArray(buffer);
                    // Hardware IDs (VID/PID)
                    if (SetupDiGetDeviceRegistryProperty(hDevInfo, &devInfoData, SPDRP_HARDWAREID, NULL, (PBYTE)buffer, sizeof(buffer), NULL))
                        info.hardwareIDs = QString::fromWCharArray(buffer);
                }
                delete[] devInterfaceDetailData;
            }
            SetupDiDestroyDeviceInfoList(hDevInfo);
        }
    }

    // --- Очистка ---
    for (UINT32 i = 0; i < count; i++) { ppDevices[i]->Release(); }
    CoTaskMemFree(ppDevices);
    pAttributes->Release();
    MFShutdown();

    return info;
}



