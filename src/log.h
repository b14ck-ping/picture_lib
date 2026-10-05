#ifndef PICTURE_LIB_LOG_H
#define PICTURE_LIB_LOG_H


typedef enum  {
    LOG_LEVEL_NOYIFY = 0,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARNING,
    LOG_LEVEL_DEBUG,
}   log_level_t;

void log_level_set(log_level_t lev);

void log_str(log_level_t lev, const char* format_str, ...);




#endif /* PICTURE_LIB_LOG_H */