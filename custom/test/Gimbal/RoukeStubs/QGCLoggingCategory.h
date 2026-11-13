#pragma once

// Keep the production Qt category and message filtering in standalone tests;
// only QGC's application-wide category registry is outside these test targets.
#include <QtCore/QLoggingCategory>
#define QGC_LOGGING_CATEGORY(name, ...) Q_LOGGING_CATEGORY(name, __VA_ARGS__)
