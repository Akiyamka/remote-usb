const en = {
  "app.connecting": "Connecting to device...",
  "app.connectionLostReconnecting": "Connection lost. Reconnecting...",
  "app.connectionLostReconnectingIn": "Connection lost. Reconnecting in {seconds}s",
  "status.apiMismatch":
    "Web UI API mismatch: expected v{expected}, device reports v{actual}. Rebuild and reflash the web UI partition.",
  "notFound.title": "404: Not Found",
  "notFound.message": "It's gone :(",

  "emulation.eyebrow": "USB storage mode",
  "emulation.title": "Card is exposed to the USB host",
  "emulation.description":
    "File operations are disabled while the host owns the SD card. Switch to HTTP mode only when the host is idle or safely ejected.",
  "emulation.deviceIp": "Device IP: {ip}",
  "emulation.switchToHttp": "Switch to HTTP file manager",

  "fileManager.dropOverlay": "Drop files to upload here",
  "fileManager.dismissError": "Dismiss error",
  "fileManager.directoryContent": "Directory content",
  "fileManager.emptyDirectory": "This directory is empty.",
  "fileManager.folder": "Folder",
  "fileManager.actionsFor": "Actions for {name}",
  "fileManager.download": "Download",
  "fileManager.delete": "Delete",
  "fileManager.confirmDelete": "Delete \"{name}\"?",

  "navigation.rootDirectory": "Root directory",
  "navigation.goToRootDirectory": "Go to root directory",
  "navigation.parentDirectory": "Parent directory",
  "navigation.goToParentDirectory": "Go to parent directory",
  "navigation.currentPath": "Current path",
  "navigation.openPath": "Open path",
  "navigation.cancelEditing": "Cancel editing",
  "navigation.editCurrentPath": "Edit current path",
  "navigation.editPath": "Edit path",
  "navigation.createDirectory": "Create directory",
  "navigation.newDirectoryPath": "New directory path",

  "upload.uploadFiles": "Upload files",
  "upload.closeDialog": "Close upload dialog",
  "upload.noUploads": "No uploads yet.",
  "upload.cancelUpload": "Cancel upload",
  "upload.selectFiles": "Select files",
  "upload.clearFinished": "Clear finished",
  "upload.status.queued": "queued",
  "upload.status.uploading": "uploading",
  "upload.status.done": "done",
  "upload.status.failed": "failed",
  "upload.status.cancelled": "cancelled",
  "upload.failedFor": "Upload failed for \"{name}\": {message}",

  "modeSwitch.switching": "Switching...",
  "modeSwitch.switchToUsb": "Switch to USB Storage mode",
  "modeSwitch.switchToHttp": "Switch to HTTP file manager",
  "modeSwitch.uploadsRunning": "Uploads are still running.",
  "modeSwitch.hostIoActive": "The USB host is still using the card. Try again after it goes idle.",
  "modeSwitch.hostReadingWriting": "The USB host is still reading or writing. Eject or wait, then try again.",
  "modeSwitch.switchInProgress": "Another mode switch is already running. Try again in a moment.",
  "modeSwitch.deviceBusy": "The device is busy. Try again in a moment.",
  "modeSwitch.transferredFilesRemain": "{message} The transferred files are still on the SD card.",

  "error.unknown": "unknown",
  "error.invalidPath": "Invalid path.",
  "error.invalidDirectoryPath": "Invalid directory path.",
  "error.directoryNameRequired": "Directory name is required.",
  "error.modeMismatch": "The SD card is in USB mode. Switch to HTTP mode first.",
  "error.notEnoughSpace": "The SD card does not have enough free space.",
  "error.uploadAlreadyRunning": "An upload is already running.",
  "error.hostUsingCard": "The USB host is still using the card.",
  "error.modeSwitchAlreadyRunning": "A mode switch is already running.",
  "error.requestFailed": "request failed",
} as const;

const ru = {
  "app.connecting": "Подключение к устройству...",
  "app.connectionLostReconnecting": "Соединение потеряно. Переподключение...",
  "app.connectionLostReconnectingIn": "Соединение потеряно. Переподключение через {seconds} с",
  "status.apiMismatch":
    "Несовпадение API Web UI: ожидалась версия {expected}, устройство сообщает версию {actual}. Пересоберите и перепрошейте раздел Web UI.",
  "notFound.title": "404: не найдено",
  "notFound.message": "Страница исчезла :(",

  "emulation.eyebrow": "Режим USB-накопителя",
  "emulation.title": "Карта доступна USB-хосту",
  "emulation.description":
    "Файловые операции отключены, пока SD-карта занята хостом. Переключайтесь в HTTP-режим только когда хост бездействует или карта безопасно извлечена.",
  "emulation.deviceIp": "IP устройства: {ip}",
  "emulation.switchToHttp": "Перейти в HTTP-файловый менеджер",

  "fileManager.dropOverlay": "Отпустите файлы здесь, чтобы загрузить",
  "fileManager.dismissError": "Закрыть ошибку",
  "fileManager.directoryContent": "Содержимое директории",
  "fileManager.emptyDirectory": "Эта директория пуста.",
  "fileManager.folder": "Папка",
  "fileManager.actionsFor": "Действия для {name}",
  "fileManager.download": "Скачать",
  "fileManager.delete": "Удалить",
  "fileManager.confirmDelete": "Удалить \"{name}\"?",

  "navigation.rootDirectory": "Корневая директория",
  "navigation.goToRootDirectory": "Перейти в корневую директорию",
  "navigation.parentDirectory": "Родительская директория",
  "navigation.goToParentDirectory": "Перейти в родительскую директорию",
  "navigation.currentPath": "Текущий путь",
  "navigation.openPath": "Открыть путь",
  "navigation.cancelEditing": "Отменить редактирование",
  "navigation.editCurrentPath": "Редактировать текущий путь",
  "navigation.editPath": "Редактировать путь",
  "navigation.createDirectory": "Создать директорию",
  "navigation.newDirectoryPath": "Путь новой директории",

  "upload.uploadFiles": "Загрузить файлы",
  "upload.closeDialog": "Закрыть окно загрузки",
  "upload.noUploads": "Загрузок пока нет.",
  "upload.cancelUpload": "Отменить загрузку",
  "upload.selectFiles": "Выбрать файлы",
  "upload.clearFinished": "Очистить завершённые",
  "upload.status.queued": "в очереди",
  "upload.status.uploading": "загружается",
  "upload.status.done": "готово",
  "upload.status.failed": "ошибка",
  "upload.status.cancelled": "отменено",
  "upload.failedFor": "Не удалось загрузить \"{name}\": {message}",

  "modeSwitch.switching": "Переключение...",
  "modeSwitch.switchToUsb": "Перейти в режим USB-накопителя",
  "modeSwitch.switchToHttp": "Перейти в HTTP-файловый менеджер",
  "modeSwitch.uploadsRunning": "Загрузка файлов ещё выполняется.",
  "modeSwitch.hostIoActive": "USB-хост всё ещё использует карту. Попробуйте снова, когда он закончит.",
  "modeSwitch.hostReadingWriting": "USB-хост всё ещё читает или записывает данные. Извлеките карту или подождите и попробуйте снова.",
  "modeSwitch.switchInProgress": "Другое переключение режима уже выполняется. Попробуйте через несколько секунд.",
  "modeSwitch.deviceBusy": "Устройство занято. Попробуйте через несколько секунд.",
  "modeSwitch.transferredFilesRemain": "{message} Переданные файлы всё ещё находятся на SD-карте.",

  "error.unknown": "неизвестно",
  "error.invalidPath": "Недопустимый путь.",
  "error.invalidDirectoryPath": "Недопустимый путь директории.",
  "error.directoryNameRequired": "Необходимо имя директории.",
  "error.modeMismatch": "SD-карта находится в USB-режиме. Сначала переключитесь в HTTP-режим.",
  "error.notEnoughSpace": "На SD-карте недостаточно свободного места.",
  "error.uploadAlreadyRunning": "Загрузка уже выполняется.",
  "error.hostUsingCard": "USB-хост всё ещё использует карту.",
  "error.modeSwitchAlreadyRunning": "Переключение режима уже выполняется.",
  "error.requestFailed": "запрос не выполнен",
} satisfies Record<keyof typeof en, string>;

type InterpolationValue = string | number | null | undefined;

const messages = { en, ru } as const;

export type Language = keyof typeof messages;
export type MessageKey = keyof typeof en;

export const currentLanguage: Language = detectLanguage();
export const currentLocale = currentLanguage === "ru" ? "ru-RU" : "en-US";

export function t(key: MessageKey, values: Record<string, InterpolationValue> = {}): string {
  return messages[currentLanguage][key].replace(/\{(\w+)\}/g, (match, name: string) => {
    const value = values[name];
    return value === null || value === undefined ? match : String(value);
  });
}

function detectLanguage(): Language {
  if (typeof navigator === "undefined") {
    return "en";
  }

  const languages = navigator.languages.length > 0 ? navigator.languages : [navigator.language];
  return languages.some((language) => language.toLowerCase().startsWith("ru")) ? "ru" : "en";
}
