/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the MIT license as published by the Free Software
 * Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 */

#include "benchData.h"
#include "bench.h"

const char charset[] =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ1234567890";

const char* locations[] = {"California.SanFrancisco", "California.LosAngles", "California.SanDiego",
                           "California.SanJose", "California.PaloAlto", "California.Campbell", "California.MountainView",
                           "California.Sunnyvale", "California.SantaClara", "California.Cupertino"};

const char* locations_sml[] = {"California.SanFrancisco", "California.LosAngles", "California.SanDiego",
                           "California.SanJose", "California.PaloAlto", "California.Campbell", "California.MountainView",
                           "California.Sunnyvale", "California.SantaClara", "California.Cupertino"};

#ifdef WINDOWS
    #define ssize_t int
    #if _MSC_VER >= 1910
        #include "benchLocations.h"
    #else
        #include "benchLocationsWin.h"
    #endif
#else
    #include "benchLocations.h"
#endif

static int usc2utf8(char *p, int unic) {
    int ret = 0;
    if (unic <= 0x0000007F) {
        *p = (unic & 0x7F);
        ret = 1;
    } else if (unic <= 0x000007FF) {
        *(p + 1) = (unic & 0x3F) | 0x80;
        *p = ((unic >> 6) & 0x1F) | 0xC0;
        ret = 2;
    } else if (unic <= 0x0000FFFF) {
        *(p + 2) = (unic & 0x3F) | 0x80;
        *(p + 1) = ((unic >> 6) & 0x3F) | 0x80;
        *p = ((unic >> 12) & 0x0F) | 0xE0;
        ret = 3;
    } else if (unic <= 0x001FFFFF) {
        *(p + 3) = (unic & 0x3F) | 0x80;
        *(p + 2) = ((unic >> 6) & 0x3F) | 0x80;
        *(p + 1) = ((unic >> 12) & 0x3F) | 0x80;
        *p = ((unic >> 18) & 0x07) | 0xF0;
        ret = 4;
    } else if (unic <= 0x03FFFFFF) {
        *(p + 4) = (unic & 0x3F) | 0x80;
        *(p + 3) = ((unic >> 6) & 0x3F) | 0x80;
        *(p + 2) = ((unic >> 12) & 0x3F) | 0x80;
        *(p + 1) = ((unic >> 18) & 0x3F) | 0x80;
        *p = ((unic >> 24) & 0x03) | 0xF8;
        ret = 5;
    // } else if (unic >= 0x04000000) {
    } else {
        *(p + 5) = (unic & 0x3F) | 0x80;
        *(p + 4) = ((unic >> 6) & 0x3F) | 0x80;
        *(p + 3) = ((unic >> 12) & 0x3F) | 0x80;
        *(p + 2) = ((unic >> 18) & 0x3F) | 0x80;
        *(p + 1) = ((unic >> 24) & 0x3F) | 0x80;
        *p = ((unic >> 30) & 0x01) | 0xFC;
        ret = 6;
    }

    return ret;
}

static void rand_string(char *str, int size, bool chinese) {
    if (chinese) {
        char *pstr = str;
        while (size > 0) {
            // Chinese Character need 3 bytes space
            if (size < 3) {
                break;
            }
            // Basic Chinese Character's Unicode is from 0x4e00 to 0x9fa5
            int unic = 0x4e00 + taosRandom() % (0x9fa5 - 0x4e00);
            int move = usc2utf8(pstr, unic);
            pstr += move;
            size -= move;
        }
    } else {
        str[0] = 0;
        if (size > 0) {
            //--size;
            int n;
            for (n = 0; n < size; n++) {
                int key = taosRandom() % (unsigned int)(sizeof(charset) - 1);
                str[n] = charset[key];
            }
            str[n] = 0;
        }
    }
}

int prepareStmt(SSuperTable *stbInfo, TAOS_STMT *stmt, uint64_t tableSeq) {
    int   len = 0;
    char *prepare = benchCalloc(1, BUFFER_SIZE, true);
    if (stbInfo->autoCreateTable) {
        char ttl[20] = "";
        if (stbInfo->ttl != 0) {
            sprintf(ttl, "TTL %d", stbInfo->ttl);
        }
        len += sprintf(prepare + len,
                       "INSERT INTO ? USING `%s` TAGS (%s) %s VALUES(?",
                       stbInfo->stbName,
                       stbInfo->tagDataBuf + stbInfo->lenOfTags * tableSeq,
                       ttl);
    } else {
        len += sprintf(prepare + len, "INSERT INTO ? VALUES(?");
    }

    for (int col = 0; col < stbInfo->cols->size; col++) {
        len += sprintf(prepare + len, ",?");
    }
    sprintf(prepare + len, ")");
    if (g_arguments->prepared_rand < g_arguments->reqPerReq) {
        infoPrint(
                  "in stmt mode, batch size(%u) can not larger than prepared "
                  "sample data size(%" PRId64
                  "), restart with larger prepared_rand or batch size will be "
                  "auto set to %" PRId64 "\n",
                  g_arguments->reqPerReq, g_arguments->prepared_rand,
                  g_arguments->prepared_rand);
        g_arguments->reqPerReq = g_arguments->prepared_rand;
    }
    if (taos_stmt_prepare(stmt, prepare, strlen(prepare))) {
        errorPrint("taos_stmt_prepare(%s) failed\n", prepare);
        tmfree(prepare);
        return -1;
    }
    tmfree(prepare);
    return 0;
}

static int generateSampleFromCsvForStb(char *buffer, char *file, int32_t length,
                                       int64_t size) {
    size_t  n = 0;
    char *  line = NULL;
    int     getRows = 0;

    FILE *fp = fopen(file, "r");
    if (fp == NULL) {
        errorPrint("Failed to open sample file: %s, reason:%s\n", file,
                   strerror(errno));
        return -1;
    }
    while (1) {
        ssize_t readLen = 0;
#if defined(WIN32) || defined(WIN64)
        toolsGetLineFile(&line, &n, fp);
        readLen = n;
#else
        readLen = getline(&line, &n, fp);
#endif
        if (-1 == readLen) {
            if (0 != fseek(fp, 0, SEEK_SET)) {
                errorPrint("Failed to fseek file: %s, reason:%s\n",
                        file, strerror(errno));
                fclose(fp);
                return -1;
            }
            continue;
        }

        if (('\r' == line[readLen - 1]) || ('\n' == line[readLen - 1])) {
            line[--readLen] = 0;
        }

        if (readLen == 0) {
            continue;
        }

        if (readLen > length) {
            infoPrint(
                "sample row len[%d] overflow define schema len[%d], so discard "
                "this row\n",
                (int32_t)readLen, length);
            continue;
        }

        memcpy(buffer + getRows * length, line, readLen);
        getRows++;

        if (getRows == size) {
            break;
        }
    }

    fclose(fp);
    tmfree(line);
    return 0;
}

static int getAndSetRowsFromCsvFile(SSuperTable *stbInfo) {
    FILE *  fp = fopen(stbInfo->sampleFile, "r");
    if (NULL == fp) {
        errorPrint("Failed to open sample file: %s, reason:%s\n",
                   stbInfo->sampleFile, strerror(errno));
        return -1;
    }

    int     line_count = 0;
    char *  buf = NULL;

    buf = benchCalloc(1, TSDB_MAX_SQL_LEN, false);
    if (NULL == buf) {
        errorPrint("%s() failed to allocate memory!\n", __func__);
        fclose(fp);
        return -1;
    }

    while (fgets(buf, TSDB_MAX_SQL_LEN, fp)) {
        line_count++;
    }
    stbInfo->insertRows = line_count;
    fclose(fp);
    tmfree(buf);
    return 0;
}

static uint32_t calcRowLen(BArray *fields, int iface) {
    uint32_t ret = 0;
    for (int i = 0; i < fields->size; ++i) {
        Field *field = benchArrayGet(fields, i);
        switch (field->type) {
            case TSDB_DATA_TYPE_BINARY:
            case TSDB_DATA_TYPE_NCHAR:
                ret += field->length + 3;
                break;
            case TSDB_DATA_TYPE_INT:
            case TSDB_DATA_TYPE_UINT:
                ret += INT_BUFF_LEN;
                break;

            case TSDB_DATA_TYPE_BIGINT:
            case TSDB_DATA_TYPE_UBIGINT:
                ret += BIGINT_BUFF_LEN;
                break;

            case TSDB_DATA_TYPE_SMALLINT:
            case TSDB_DATA_TYPE_USMALLINT:
                ret += SMALLINT_BUFF_LEN;
                break;

            case TSDB_DATA_TYPE_TINYINT:
            case TSDB_DATA_TYPE_UTINYINT:
                ret += TINYINT_BUFF_LEN;
                break;

            case TSDB_DATA_TYPE_BOOL:
                ret += BOOL_BUFF_LEN;
                break;

            case TSDB_DATA_TYPE_FLOAT:
                ret += FLOAT_BUFF_LEN;
                break;

            case TSDB_DATA_TYPE_DOUBLE:
                ret += DOUBLE_BUFF_LEN;
                break;

            case TSDB_DATA_TYPE_TIMESTAMP:
                ret += TIMESTAMP_BUFF_LEN;
                break;
            case TSDB_DATA_TYPE_JSON:
                ret += (JSON_BUFF_LEN + field->length) * fields->size;
                return ret;
        }
        ret += 1;
        if (iface == SML_REST_IFACE || iface == SML_IFACE) {
            ret += SML_LINE_SQL_SYNTAX_OFFSET + strlen(field->name);
        }
    }
    if (iface == SML_IFACE || iface == SML_REST_IFACE) {
        ret += 2 * TSDB_TABLE_NAME_LEN * 2 + SML_LINE_SQL_SYNTAX_OFFSET;
    }
    ret += TIMESTAMP_BUFF_LEN;
    return ret;
}

int generateRandData(SSuperTable *stbInfo, char *sampleDataBuf,
                      int lenOfOneRow, BArray * fields, int64_t loop,
                      bool tag) {
    int     iface = stbInfo->iface;
    int     line_protocol = stbInfo->lineProtocol;
    if (iface == STMT_IFACE) {
        for (int i = 0; i < fields->size; ++i) {
            Field * field = benchArrayGet(fields, i);
            if (field->type == TSDB_DATA_TYPE_BINARY ||
                    field->type == TSDB_DATA_TYPE_NCHAR) {
                field->data = benchCalloc(1, loop * (field->length + 1), true);
            } else {
                field->data = benchCalloc(1, loop * field->length, true);
            }
        }
    }
    for (int64_t k = 0; k < loop; ++k) {
        int64_t pos = k * lenOfOneRow;
        if (line_protocol == TSDB_SML_LINE_PROTOCOL &&
            (iface == SML_IFACE || iface == SML_REST_IFACE) && tag) {
            pos += sprintf(sampleDataBuf + pos, "%s,", stbInfo->stbName);
        }
        for (int i = 0; i < fields->size; ++i) {
            Field * field = benchArrayGet(fields, i);
            if (iface == TAOSC_IFACE || iface == REST_IFACE) {
                if (field->none) {
                    continue;
                }
                if (field->null) {
                    pos += sprintf(sampleDataBuf + pos, "null,");
                    continue;
                }
                if (field->type == TSDB_DATA_TYPE_TIMESTAMP && !tag) {
                    pos += sprintf(sampleDataBuf + pos, "now,");
                    continue;
                }
            }
            switch (field->type) {
                case TSDB_DATA_TYPE_BOOL: {
                    bool rand_bool = (taosRandom() % 2) & 1;
                    if (iface == STMT_IFACE) {
                        ((bool *)field->data)[k] = rand_bool;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%s,",
                                       field->name,
                                       rand_bool ? "true" : "false");
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (tag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%s ",
                                           field->name,
                                           rand_bool ? "true" : "false");
                        } else {
                            pos += sprintf(sampleDataBuf + pos, "%s ",
                                           rand_bool ? "true" : "false");
                        }
                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%s,",
                                       rand_bool ? "true" : "false");
                    }
                    break;
                }
                case TSDB_DATA_TYPE_TINYINT: {
                    int8_t tinyint =
                            field->min +
                        (taosRandom() % (field->max - field->min));
                    if (iface == STMT_IFACE) {
                        ((int8_t *)field->data)[k] = tinyint;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%di8,",
                                       field->name, tinyint);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (tag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%di8 ",
                                           field->name, tinyint);
                        } else {
                            pos +=
                                sprintf(sampleDataBuf + pos, "%di8 ", tinyint);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%d,", tinyint);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_UTINYINT: {
                    uint8_t utinyint = field->min + (taosRandom() % (field->max - field->min));
                    if (iface == STMT_IFACE) {
                        ((uint8_t *)field->data)[k] = utinyint;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%uu8,",
                                       field->name, utinyint);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (tag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%uu8 ",
                                           field->name, utinyint);
                        } else {
                            pos += sprintf(sampleDataBuf + pos, "%uu8 ", utinyint);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%u,", utinyint);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_SMALLINT: {
                    int16_t smallint = field->min + (taosRandom() % (field->max -field->min));
                    if (iface == STMT_IFACE) {
                        ((int16_t *)field->data)[k] = smallint;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%di16,",
                                       field->name, smallint);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (tag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%di16 ",
                                           field->name, smallint);
                        } else {
                            pos += sprintf(sampleDataBuf + pos, "%di16 ",
                                           smallint);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%d,", smallint);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_USMALLINT: {
                    uint16_t usmallint = field->min
                        + (taosRandom() % (field->max - field->min));
                    if (iface == STMT_IFACE) {
                        ((uint16_t *)field->data)[k] = usmallint;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%uu16,",
                                       field->name, usmallint);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (tag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%uu16 ",
                                           field->name, usmallint);
                        } else {
                            pos += sprintf(sampleDataBuf + pos, "%uu16 ",
                                           usmallint);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%u,", usmallint);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_INT: {
                    int32_t int_;
                    if ((g_arguments->demo_mode) && (i == 0)) {
                        unsigned int tmpRand = taosRandom();
                        int_ = tmpRand % 10 + 1;
                    } else if ((g_arguments->demo_mode) && (i == 1)) {
                        int_ = 105 + taosRandom() % 10;
                    } else {
                        if (field->min < (-1 * (RAND_MAX >> 1))) {
                            field->min = -1 * (RAND_MAX >> 1);
                        }
                        if (field->max > (RAND_MAX >> 1)) {
                            field->max = RAND_MAX >> 1;
                        }
                        int_ = field->min + (taosRandom() % (field->max - field->min));
                    }
                    if (iface == STMT_IFACE) {
                        ((int32_t *)field->data)[k] = int_;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%di32,",
                                       field->name, int_);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (tag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%di32 ",
                                           field->name, int_);
                        } else {
                            pos += sprintf(sampleDataBuf + pos, "%di32 ", int_);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%d,", int_);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_BIGINT: {
                    int64_t _bigint;
                    _bigint = field->min + (taosRandom() % (field->max - field->min));
                    if (iface == STMT_IFACE) {
                        ((int64_t *)field->data)[k] = _bigint;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%"PRId64"i64,",
                                       field->name, _bigint);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (tag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%"PRId64"i64 ",
                                           field->name, _bigint);
                        } else {
                            pos += sprintf(sampleDataBuf + pos, "%"PRId64"i64 ", _bigint);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%"PRId64",", _bigint);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_UINT: {
                    uint32_t _uint = field->min + (taosRandom() % (field->max - field->min));
                    if (iface == STMT_IFACE) {
                        ((uint32_t *)field->data)[k] = _uint;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%uu32,",
                                       field->name, _uint);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (tag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%uu32 ",
                                           field->name, _uint);
                        } else {
                            pos += sprintf(sampleDataBuf + pos, "%uu32 ", _uint);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%u,", _uint);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_UBIGINT:
                case TSDB_DATA_TYPE_TIMESTAMP: {
                    uint64_t _ubigint =
                            field->min +
                        (taosRandom() % (field->max - field->min));
                    if (iface == STMT_IFACE) {
                        ((uint64_t *)field->data)[k] = _ubigint;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%"PRIu64"u64,",
                                       field->name, _ubigint);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (tag) {
                            pos += sprintf(sampleDataBuf + pos,
                                           "%s=%"PRIu64"u64 ",
                                           field->name, _ubigint);
                        } else {
                            pos +=
                                sprintf(sampleDataBuf + pos,
                                        "%"PRIu64"u64 ", _ubigint);
                        }
                    } else {
                        pos += sprintf(sampleDataBuf + pos,
                                       "%"PRIu64",", _ubigint);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_FLOAT: {
                    float _float = (float)(field->min +
                                           (taosRandom() %
                                            (field->max - field->min)) +
                                           (taosRandom() % 1000) / 1000.0);
                    if (g_arguments->demo_mode && i == 0) {
                        _float = (float)(9.8 + 0.04 * (taosRandom() % 10) +
                                         _float / 1000000000);
                    } else if (g_arguments->demo_mode && i == 2) {
                        _float = (float)((105 + taosRandom() % 10 +
                                          _float / 1000000000) /
                                         360);
                    }
                    if (iface == STMT_IFACE) {
                        ((float *)(field->data))[k] = _float;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%ff32,",
                                       field->name, _float);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (tag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%ff32 ",
                                           field->name, _float);
                        } else {
                            pos +=
                                sprintf(sampleDataBuf + pos, "%ff32 ", _float);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%f,", _float);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_DOUBLE: {
                    double double_ =
                        (double)(field->min +
                                 (taosRandom() %
                                  (field->max - field->min)) +
                                 taosRandom() % 1000000 / 1000000.0);
                    if (iface == STMT_IFACE) {
                        ((double *)field->data)[k] = double_;
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=%ff64,",
                                       field->name, double_);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (tag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=%ff64 ",
                                           field->name, double_);
                        } else {
                            pos +=
                                sprintf(sampleDataBuf + pos, "%ff64 ", double_);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "%f,", double_);
                    }
                    break;
                }
                case TSDB_DATA_TYPE_BINARY:
                case TSDB_DATA_TYPE_NCHAR: {
                    char *tmp = benchCalloc(1, field->length + 1, false);
                    if (g_arguments->demo_mode) {
                        unsigned int tmpRand = taosRandom();
                        if (g_arguments->chinese) {
                            sprintf(tmp, "%s", locations_chinese[tmpRand % 10]);
                        } else if (stbInfo->iface == SML_IFACE) {
                            sprintf(tmp, "%s", locations_sml[tmpRand % 10]);
                        } else {
                            sprintf(tmp, "%s", locations[tmpRand % 10]);
                        }
                    } else if (field->values) {
                        int arraySize = tools_cJSON_GetArraySize(field->values);
                        if (arraySize) {
                            tools_cJSON *buf = tools_cJSON_GetArrayItem(
                                    field->values,
                                    taosRandom() % arraySize);
                            snprintf(tmp, field->length, "%s", buf->valuestring);
                        } else {
                            errorPrint("%s() cannot read correct value from json file. array size: %d\n",
                                    __func__, arraySize);
                            free(tmp);
                            return -1;
                        }
                    } else {
                        rand_string(tmp, field->length,
                                    g_arguments->chinese);
                    }
                    if (iface == STMT_IFACE) {
                        sprintf((char *)field->data + k * field->length,
                                "%s", tmp);
                    }
                    if ((iface == SML_IFACE || iface == SML_REST_IFACE) &&
                            field->type == TSDB_DATA_TYPE_BINARY &&
                        line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=\"%s\",",
                                       field->name, tmp);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                            field->type == TSDB_DATA_TYPE_NCHAR &&
                               line_protocol == TSDB_SML_LINE_PROTOCOL) {
                        pos += sprintf(sampleDataBuf + pos, "%s=L\"%s\",",
                                       field->name, tmp);
                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                            field->type == TSDB_DATA_TYPE_BINARY &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (tag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=L\"%s\" ",
                                           field->name, tmp);
                        } else {
                            pos += sprintf(sampleDataBuf + pos, "\"%s\" ", tmp);
                        }

                    } else if ((iface == SML_IFACE ||
                                iface == SML_REST_IFACE) &&
                            field->type == TSDB_DATA_TYPE_NCHAR &&
                               line_protocol == TSDB_SML_TELNET_PROTOCOL) {
                        if (tag) {
                            pos += sprintf(sampleDataBuf + pos, "%s=L\"%s\" ",
                                           field->name, tmp);
                        } else {
                            pos +=
                                sprintf(sampleDataBuf + pos, "L\"%s\" ", tmp);
                        }

                    } else {
                        pos += sprintf(sampleDataBuf + pos, "'%s',", tmp);
                    }
                    tmfree(tmp);
                    break;
                }
                case TSDB_DATA_TYPE_JSON: {
                    pos += sprintf(sampleDataBuf + pos, "'{");
                    for (int j = 0; j < fields->size; ++j) {
                        pos += sprintf(sampleDataBuf + pos, "\"k%d\":", j);
                        char *buf = benchCalloc(1, field->length + 1, false);
                        rand_string(buf, field->length,
                                    g_arguments->chinese);
                        pos += sprintf(sampleDataBuf + pos, "\"%s\",", buf);
                        tmfree(buf);
                    }
                    pos += sprintf(sampleDataBuf + pos - 1, "}'");
                    goto skip;
                }
            }
        }
skip:
        *(sampleDataBuf + pos - 1) = 0;
    }

    return 0;
}

int prepareSampleData(SDataBase* database, SSuperTable* stbInfo) {
    stbInfo->lenOfCols = calcRowLen(stbInfo->cols, stbInfo->iface);
    stbInfo->lenOfTags = calcRowLen(stbInfo->tags, stbInfo->iface);
    if (stbInfo->partialColNum != 0 &&
        (stbInfo->iface == TAOSC_IFACE || stbInfo->iface == REST_IFACE)) {
        if (stbInfo->partialColNum > stbInfo->cols->size) {
            stbInfo->partialColNum = stbInfo->cols->size;
        } else {
            stbInfo->partialColNameBuf = benchCalloc(1, BUFFER_SIZE, true);
            int pos = 0;
            pos += sprintf(stbInfo->partialColNameBuf + pos, "ts");
            for (int i = 0; i < stbInfo->partialColNum; ++i) {
                Field * col = benchArrayGet(stbInfo->cols, i);
                pos += sprintf(stbInfo->partialColNameBuf + pos, ",%s", col->name);
            }
            for (int i = stbInfo->partialColNum; i < stbInfo->cols->size; ++i) {
                Field * col = benchArrayGet(stbInfo->cols, i);
                col->none = true;
            }
            debugPrint("partialColNameBuf: %s\n",
                       stbInfo->partialColNameBuf);
        }
    } else {
        stbInfo->partialColNum = stbInfo->cols->size;
    }
    stbInfo->sampleDataBuf =
            benchCalloc(1, stbInfo->lenOfCols * g_arguments->prepared_rand, true);
    infoPrint(
              "generate stable<%s> columns data with lenOfCols<%u> * "
              "prepared_rand<%" PRIu64 ">\n",
              stbInfo->stbName, stbInfo->lenOfCols, g_arguments->prepared_rand);
    if (stbInfo->random_data_source) {
        if (generateRandData(stbInfo, stbInfo->sampleDataBuf, stbInfo->lenOfCols,
                         stbInfo->cols, g_arguments->prepared_rand, false)) {
            return -1;
        }
    } else {
        if (stbInfo->useSampleTs) {
            if (getAndSetRowsFromCsvFile(stbInfo)) return -1;
        }
        if (generateSampleFromCsvForStb(stbInfo->sampleDataBuf,
                                        stbInfo->sampleFile, stbInfo->lenOfCols,
                                        g_arguments->prepared_rand)) {
            errorPrint("Failed to generate sample from csv file %s\n",
                    stbInfo->sampleFile);
            return -1;
        }
    }
    debugPrint("sampleDataBuf: %s\n", stbInfo->sampleDataBuf);

    if (!stbInfo->childTblExists && stbInfo->tags->size != 0) {
        stbInfo->tagDataBuf =
                benchCalloc(1, stbInfo->childTblCount * stbInfo->lenOfTags, true);
        infoPrint(
                  "generate stable<%s> tags data with lenOfTags<%u> * "
                  "childTblCount<%" PRIu64 ">\n",
                  stbInfo->stbName, stbInfo->lenOfTags, stbInfo->childTblCount);
        if (stbInfo->tagsFile[0] != 0) {
            if (generateSampleFromCsvForStb(
                    stbInfo->tagDataBuf, stbInfo->tagsFile, stbInfo->lenOfTags,
                    stbInfo->childTblCount)) {
                return -1;
            }
        } else {
            if (generateRandData(stbInfo, stbInfo->tagDataBuf, stbInfo->lenOfTags,
                             stbInfo->tags, stbInfo->childTblCount, true)) {
                return -1;
            }
        }
        debugPrint("tagDataBuf: %s\n", stbInfo->tagDataBuf);
    }

    if (0 != convertServAddr(stbInfo->iface,
                       stbInfo->tcpTransfer, stbInfo->lineProtocol)) {
        return -1;
    }
    return 0;
}

int64_t getTSRandTail(int64_t timeStampStep, int32_t seq, int disorderRatio,
                      int disorderRange) {
    int64_t randTail = timeStampStep * seq;
    if (disorderRatio > 0) {
        int rand_num = taosRandom() % 100;
        if (rand_num < disorderRatio) {
            randTail = (randTail + (taosRandom() % disorderRange + 1)) * (-1);
        }
    }
    return randTail;
}

uint32_t bindParamBatch(threadInfo *pThreadInfo, uint32_t batch, int64_t startTime) {
    TAOS_STMT *  stmt = pThreadInfo->conn->stmt;
    SSuperTable *stbInfo = pThreadInfo->stbInfo;
    uint32_t     columnCount = stbInfo->cols->size;
    memset(pThreadInfo->bindParams, 0,
           (sizeof(TAOS_MULTI_BIND) * (columnCount + 1)));
    memset(pThreadInfo->is_null, 0, batch);

    for (int c = 0; c < columnCount + 1; c++) {
        TAOS_MULTI_BIND *param =
            (TAOS_MULTI_BIND *)(pThreadInfo->bindParams +
                                sizeof(TAOS_MULTI_BIND) * c);

        char data_type;

        if (c == 0) {
            data_type = TSDB_DATA_TYPE_TIMESTAMP;
            param->buffer_length = sizeof(int64_t);
            param->buffer = pThreadInfo->bind_ts_array;

        } else {
            Field * col = benchArrayGet(stbInfo->cols, c - 1);
            data_type = col->type;
            param->buffer = col->data;
            param->buffer_length = col->length;
            debugPrint("col[%d]: type: %s, len: %d\n", c,
                       convertDatatypeToString(data_type),
                       col->length);
            param->is_null = col->is_null;
        }
        param->buffer_type = data_type;
        param->length = benchCalloc(batch, sizeof(int32_t), true);

        for (int b = 0; b < batch; b++) {
            param->length[b] = (int32_t)param->buffer_length;
        }
        param->num = batch;
    }

    for (uint32_t k = 0; k < batch; k++) {
        /* columnCount + 1 (ts) */
        if (stbInfo->disorderRatio) {
            *(pThreadInfo->bind_ts_array + k) =
                startTime + getTSRandTail(stbInfo->timestamp_step, k,
                                          stbInfo->disorderRatio,
                                          stbInfo->disorderRange);
        } else {
            *(pThreadInfo->bind_ts_array + k) =
                startTime + stbInfo->timestamp_step * k;
        }
    }

    if (taos_stmt_bind_param_batch(
            stmt, (TAOS_MULTI_BIND *)pThreadInfo->bindParams)) {
        errorPrint("taos_stmt_bind_param_batch() failed! reason: %s\n",
                   taos_stmt_errstr(stmt));
        return 0;
    }

    for (int c = 0; c < stbInfo->cols->size + 1; c++) {
        TAOS_MULTI_BIND *param =
            (TAOS_MULTI_BIND *)(pThreadInfo->bindParams +
                                sizeof(TAOS_MULTI_BIND) * c);
        tmfree(param->length);
    }

    // if msg > 3MB, break
    if (taos_stmt_add_batch(stmt)) {
        errorPrint("taos_stmt_add_batch() failed! reason: %s\n",
                   taos_stmt_errstr(stmt));
        return 0;
    }
    return batch;
}

void generateSmlJsonTags(tools_cJSON *tagsList, SSuperTable *stbInfo,
                            uint64_t start_table_from, int tbSeq) {
    tools_cJSON * tags = tools_cJSON_CreateObject();
    char *  tbName = benchCalloc(1, TSDB_TABLE_NAME_LEN, true);
    snprintf(tbName, TSDB_TABLE_NAME_LEN, "%s%" PRIu64 "",
             stbInfo->childTblPrefix, tbSeq + start_table_from);
    tools_cJSON_AddStringToObject(tags, "id", tbName);
    char *tagName = benchCalloc(1, TSDB_MAX_TAGS, true);
    for (int i = 0; i < stbInfo->tags->size; i++) {
        Field * tag = benchArrayGet(stbInfo->tags, i);
        tools_cJSON *tagObj = tools_cJSON_CreateObject();
        snprintf(tagName, TSDB_MAX_TAGS, "t%d", i);
        switch (tag->type) {
            case TSDB_DATA_TYPE_BOOL: {
                tools_cJSON_AddBoolToObject(tagObj, "value", (taosRandom() % 2) & 1);
                tools_cJSON_AddStringToObject(tagObj, "type", "bool");
                break;
            }
            case TSDB_DATA_TYPE_FLOAT: {
                tools_cJSON_AddNumberToObject(
                        tagObj, "value",
                        (float)(tag->min +
                            (taosRandom() % (tag->max - tag->min)) +
                            taosRandom() % 1000 / 1000.0));
                tools_cJSON_AddStringToObject(tagObj, "type", "float");
                break;
            }
            case TSDB_DATA_TYPE_DOUBLE: {
                tools_cJSON_AddNumberToObject(
                        tagObj, "value",
                        (double)(tag->min + (taosRandom() % (tag->max - tag->min)) +
                             taosRandom() % 1000000 / 1000000.0));
                tools_cJSON_AddStringToObject(tagObj, "type", "double");
                break;
            }

            case TSDB_DATA_TYPE_BINARY:
            case TSDB_DATA_TYPE_NCHAR: {
                char *buf = (char *)benchCalloc(tag->length + 1, 1, false);
                rand_string(buf, tag->length, g_arguments->chinese);
                if (tag->type == TSDB_DATA_TYPE_BINARY) {
                    tools_cJSON_AddStringToObject(tagObj, "value", buf);
                    tools_cJSON_AddStringToObject(tagObj, "type", "binary");
                } else {
                    tools_cJSON_AddStringToObject(tagObj, "value", buf);
                    tools_cJSON_AddStringToObject(tagObj, "type", "nchar");
                }
                tmfree(buf);
                break;
            }
            default:
                tools_cJSON_AddNumberToObject(
                        tagObj, "value",
                        tag->min + (taosRandom() % (tag->max - tag->min)));
                tools_cJSON_AddStringToObject(tagObj, "type", convertDatatypeToString(tag->type));
                break;
        }
        tools_cJSON_AddItemToObject(tags, tagName, tagObj);
    }
    tools_cJSON_AddItemToArray(tagsList, tags);
    tmfree(tagName);
    tmfree(tbName);
}

void generateSmlJsonCols(tools_cJSON *array, tools_cJSON *tag,
                         SSuperTable *stbInfo,
                            uint32_t time_precision, int64_t timestamp) {
    tools_cJSON * record = tools_cJSON_CreateObject();
    tools_cJSON * ts = tools_cJSON_CreateObject();
    tools_cJSON_AddNumberToObject(ts, "value", (double)timestamp);
    if (time_precision == TSDB_SML_TIMESTAMP_MILLI_SECONDS) {
        tools_cJSON_AddStringToObject(ts, "type", "ms");
    } else if (time_precision == TSDB_SML_TIMESTAMP_MICRO_SECONDS) {
        tools_cJSON_AddStringToObject(ts, "type", "us");
    } else if (time_precision == TSDB_SML_TIMESTAMP_NANO_SECONDS) {
        tools_cJSON_AddStringToObject(ts, "type", "ns");
    }
    tools_cJSON *value = tools_cJSON_CreateObject();
    Field* col = benchArrayGet(stbInfo->cols, 0);
    switch (col->type) {
        case TSDB_DATA_TYPE_BOOL:
            tools_cJSON_AddBoolToObject(value, "value", (taosRandom()%2)&1);
            tools_cJSON_AddStringToObject(value, "type", "bool");
            break;
        case TSDB_DATA_TYPE_FLOAT:
            tools_cJSON_AddNumberToObject(
                value, "value",
                (float)(col->min +
                        (taosRandom() % (col->max - col->min)) +
                        taosRandom() % 1000 / 1000.0));
            tools_cJSON_AddStringToObject(value, "type", "float");
            break;
        case TSDB_DATA_TYPE_DOUBLE:
            tools_cJSON_AddNumberToObject(
                value, "value",
                (double)(col->min +
                         (taosRandom() % (col->max - col->min)) +
                         taosRandom() % 1000000 / 1000000.0));
            tools_cJSON_AddStringToObject(value, "type", "double");
            break;
        case TSDB_DATA_TYPE_BINARY:
        case TSDB_DATA_TYPE_NCHAR: {
            char *buf = (char *)benchCalloc(col->length + 1, 1, false);
            rand_string(buf, col->length, g_arguments->chinese);
            if (col->type == TSDB_DATA_TYPE_BINARY) {
                tools_cJSON_AddStringToObject(value, "value", buf);
                tools_cJSON_AddStringToObject(value, "type", "binary");
            } else {
                tools_cJSON_AddStringToObject(value, "value", buf);
                tools_cJSON_AddStringToObject(value, "type", "nchar");
            }
            tmfree(buf);
            break;
        }
        default:
            tools_cJSON_AddNumberToObject(
                    value, "value",
                    (double)col->min +
                    (taosRandom() % (col->max - col->min)));
            tools_cJSON_AddStringToObject(value, "type", convertDatatypeToString(col->type));
            break;
    }
    tools_cJSON_AddItemToObject(record, "timestamp", ts);
    tools_cJSON_AddItemToObject(record, "value", value);
    tools_cJSON_AddItemToObject(record, "tags", tag);
    tools_cJSON_AddStringToObject(record, "metric", stbInfo->stbName);
    tools_cJSON_AddItemToArray(array, record);
}
