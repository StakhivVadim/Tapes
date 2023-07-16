#include <iostream>
#include <fstream>
#include <vector>
#include <algorithm>
#include <string>
#include <chrono>
#include <thread>
#include <sstream>

class TapeInterface {
public:
    virtual bool read(int& value) = 0;
    virtual bool write(int value) = 0;
    virtual void rewind() = 0;
    virtual void shift() = 0;
    virtual bool isEnd() = 0;
};

class TapeDevice : public TapeInterface {
private:
    std::ifstream inputFile;
    std::ofstream outputFile;
    std::string inputFilename;
    std::string outputFilename;
    int readDelay; // Задержка чтения в миллисекундах
    int writeDelay; // Задержка записи в миллисекундах

public:
    TapeDevice(const std::string& inputFilename, const std::string& outputFilename, int readDelay, int writeDelay)
        : inputFilename(inputFilename), outputFilename(outputFilename), readDelay(readDelay), writeDelay(writeDelay) {}

    bool read(int& value) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(readDelay)); // Задержка чтения
        if (!inputFile.is_open()) {
            inputFile.open(inputFilename);
            if (!inputFile.is_open()) {
                return false; // Ошибка открытия файла
            }
        }
        if (inputFile >> value) {
            shift();
            return true; // Успешное чтение значения из файла
        }
        return false; // Достигнут конец файла
    }

    bool write(int value) override {
        std::this_thread::sleep_for(std::chrono::milliseconds(writeDelay)); // Задержка записи
        if (!outputFile.is_open()) {
            outputFile.open(outputFilename, std::ios_base::app); // Открываем файл в режиме добавления данных в конец
            if (!outputFile.is_open()) {
                return false; // Ошибка открытия файла
            }
        }
        outputFile << value << " ";
        shift();
        return true;
    }

    void rewind() override {
        inputFile.clear();
        inputFile.seekg(0, std::ios::beg);
    }

    void shift() override {
        inputFile.seekg(1, std::ios::cur);
    }


    bool isEnd() override {
        return inputFile.eof();
    }

    std::pair<int, int> readConfigFile(const std::string& configFilename) {
        std::ifstream configFile(configFilename);
        int readDelay = 0;
        int writeDelay = 0;
        if (configFile.is_open()) {
            std::string line;
            while (std::getline(configFile, line)) {
                std::istringstream iss(line);
                std::string key;
                int value;
                if (std::getline(iss, key, '=') && iss >> value) {
                    if (key == "readDelay") {
                        readDelay = value;
                    }
                    else if (key == "writeDelay") {
                        writeDelay = value;
                    }
                }
            }
            configFile.close();
        }
        return std::make_pair(readDelay, writeDelay);
    }
};

class TapeSorter {
private:
    TapeInterface& inputTape;
    TapeInterface& outputTape;
    std::string tmpDirectory;
    int memoryLimit;
    int& readDelay;
    int& writeDelay;

public:
    TapeSorter(TapeInterface& inputTape, TapeInterface& outputTape, const std::string& tmpDirectory, int memoryLimit, int& readDelay, int& writeDelay)
        : inputTape(inputTape), outputTape(outputTape), tmpDirectory(tmpDirectory), memoryLimit(memoryLimit), readDelay(readDelay), writeDelay(writeDelay) {}

    void sort() {
        std::vector<std::string> tempFiles; // Временные ленты для хранения отсортированных блоков
        int tempFileCount = 0;
        std::vector<int> buffer;
        int blockSize = 0;
        int value;
        // Чтение и сортировка блоков
        while (inputTape.read(value)) {
            buffer.push_back(value);
            // Если буфер достигает лимита памяти, сохраняем его во временный файл
            if (buffer.size() >= memoryLimit) {
                std::sort(buffer.begin(), buffer.end());
                std::string tempFileName = "tmp/temp_" + std::to_string(tempFileCount++) + ".dat";
                tempFiles.push_back(tempFileName);
                saveBufferToFile(buffer, tempFileName);
                buffer.clear();
            }
        }
        // Сортируем и сохраняем оставшийся буфер в последний временный файл
        if (!buffer.empty()) {
            std::sort(buffer.begin(), buffer.end());
            std::string tempFileName = "tmp/temp_" + std::to_string(tempFileCount) + ".dat";
            tempFiles.push_back(tempFileName);
            saveBufferToFile(buffer, tempFileName);
            buffer.clear();
        }
        // Объединяем отсортированные данные из временных файлов на выходной ленте
        mergeTempFiles(tempFiles, outputTape);
        // Удаляем временные файлы
        for (const auto& tempFile : tempFiles) {
            std::remove(tempFile.c_str());
        }
        inputTape.rewind(); // Перемотка ленты
    }

    void saveBufferToFile(const std::vector<int>& buffer, const std::string& filename) {
        std::ofstream file(filename, std::ios::binary);
        for (const auto& item : buffer) {
            file.write(reinterpret_cast<const char*>(&item), sizeof(int));
        }
        file.close();
    }

    void mergeTempFiles(const std::vector<std::string>& tempFiles, TapeInterface& outputTape) {
        std::vector<std::ifstream> fileStreams;
        std::vector<int> values(tempFiles.size(), 0);
        for (const auto& tempFile : tempFiles) {
            fileStreams.emplace_back(tempFile, std::ios::binary);
        }
        while (!fileStreams.empty()) {
            int minIndex = -1;
            int minValue = std::numeric_limits<int>::max();
            // Находим минимальное значение среди всех временных файлов
            for (int i = 0; i < fileStreams.size(); ++i) {
                // Проверяем, достиг ли файл конца
                if (!fileStreams[i].eof()) {
                    // Если значение уже было прочитано, используем его
                    if (values[i] != 0) {
                        if (values[i] < minValue) {
                            minValue = values[i];
                            minIndex = i;
                        }
                    }
                    // В противном случае, считываем новое значение из файла
                    else if (fileStreams[i].read(reinterpret_cast<char*>(&values[i]), sizeof(int))) {
                        if (values[i] < minValue) {
                            minValue = values[i];
                            minIndex = i;
                        }
                    }
                }
            }
            // Если найдено минимальное значение, записываем его на выходную ленту
            if (minIndex != -1) {
                outputTape.write(minValue);
                values[minIndex] = 0; // Обнуляем значение, чтобы прочитать новое при необходимости
            }
            // Если файл достиг конца, закрываем его и удаляем из списка
            else {
                fileStreams.back().close();
                fileStreams.pop_back();
            }
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 3) {
        std::cout << "input format: Name_of_app input.txt output.txt" << std::endl;
        return 1;
    };
    std::string inputFilename = argv[1];
    std::string outputFilename = argv[2];
    std::string tmpDirectory = "tmp";
    std::string configFilename = "config.txt";
    int memoryLimit = 100;  // Задаем ограничение по использованию оперативной памяти (в количестве элементов)
    TapeDevice tapeDevice(inputFilename, outputFilename, 0, 0);
    std::pair<int, int> delays = tapeDevice.readConfigFile(configFilename);
    int readDelay = delays.first;
    int writeDelay = delays.second;
    TapeDevice inputTape(inputFilename, outputFilename, readDelay, writeDelay);
    TapeDevice outputTape("", outputFilename, readDelay, writeDelay);
    TapeSorter sorter(inputTape, outputTape, tmpDirectory, memoryLimit, readDelay, writeDelay);
    sorter.sort();

    std::cout << "Sort is finished. Result in file: " << outputFilename << std::endl;

    return 0;
}
