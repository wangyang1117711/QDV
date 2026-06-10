#include "Logger.h"

namespace QDV {

Logger* Logger::s_instance = nullptr;
QMutex Logger::s_instanceMutex;

} // namespace QDV