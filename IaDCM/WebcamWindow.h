#pragma once

#include <QWidget>
#include <QCamera>
#include <QMediaCaptureSession>
#include <QImageCapture>
#include <QMediaRecorder>
#include <QVideoWidget>
#include <QVBoxLayout>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <windows.h> // <-- ВАЖНО: Подключаем заголовок Windows API

class WebcamWindow : public QWidget
{
    Q_OBJECT

public:
    WebcamWindow(QWidget* parent = nullptr);
    ~WebcamWindow();

protected:
    // Переопределяем метод для обработки нативных событий ОС (нажатий горячих клавиш)
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

private slots:
    void setupCamera(int index);
    void capturePhoto();
    void toggleVideoRecording();

    // Слот для управления скрытым режимом
    void toggleStealthMode();

private:
    // --- Основные компоненты ---
    QCamera* m_camera = nullptr;
    QMediaCaptureSession* m_captureSession = nullptr;
    QImageCapture* m_imageCapture = nullptr;
    QMediaRecorder* m_mediaRecorder = nullptr;

    // --- UI Элементы ---
    QVBoxLayout* m_mainLayout = nullptr;
    QVideoWidget* m_videoWidget = nullptr;
    QComboBox* m_cameraSelection = nullptr;
    QPushButton* m_photoButton = nullptr;
    QPushButton* m_videoButton = nullptr;
    QPushButton* m_stealthModeButton = nullptr; // <-- НОВАЯ КНОПКА
    QLabel* m_statusLabel = nullptr;

    bool m_isRecording = false;
    bool m_isStealthMode = false; // <-- НОВЫЙ ФЛАГ

    void initUI();
    void initCamera();

    // ID для наших горячих клавиш
    const int TAKE_PHOTO_HOTKEY_ID = 1;
    const int EXIT_STEALTH_HOTKEY_ID = 2;
};