###################################################################
#           Copyright (c) 2016 by TAOS Technologies, Inc.
#                     All rights reserved.
#
#  This file is proprietary and confidential to TAOS Technologies.
#  No part of this file may be reproduced, stored, transmitted,
#  disclosed or used in any form or by any means other than as
#  expressly provided by the written permission from Jianhui Tao
#
###################################################################

# -*- coding: utf-8 -*-
import os
import subprocess

from util.log import *
from util.cases import *
from util.sql import *
from util.dnodes import *


class TDTestCase:
    def caseDescription(self):
        """
        [TD-11510] taosBenchmark test cases
        """

    def init(self, conn, logSql):
        tdLog.debug("start to execute %s" % __file__)
        tdSql.init(conn.cursor(), logSql)

    def getPath(self, tool="taosBenchmark"):
        selfPath = os.path.dirname(os.path.realpath(__file__))

        if "community" in selfPath:
            projPath = selfPath[: selfPath.find("community")]
        elif "src" in selfPath:
            projPath = selfPath[: selfPath.find("src")]
        elif "/tools/" in selfPath:
            projPath = selfPath[: selfPath.find("/tools/")]
        elif "/tests/" in selfPath:
            projPath = selfPath[: selfPath.find("/tests/")]
        else:
            tdLog.info("cannot found %s in path: %s, use system's" % (tool, selfPath))
            projPath = "/usr/local/taos/bin/"

        paths = []
        for root, dummy, files in os.walk(projPath):
            if (tool) in files:
                rootRealPath = os.path.dirname(os.path.realpath(root))
                if "packaging" not in rootRealPath:
                    paths.append(os.path.join(root, tool))
                    break
        if len(paths) == 0:
            return ""
        return paths[0]

    def run(self):
        binPath = self.getPath()
        cmd = (
            "%s -F 7 -H 9 -n 10 -t 2 -x -y -M -C -d newtest -l 5 -A binary,nchar\(31\) -b tinyint,binary\(23\),bool,nchar -w 29 -E -m $%%^*"
            % binPath
        )
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("use newtest")
        tdSql.query("select count(*) from newtest.meters")
        tdSql.checkData(0, 0, 20)
        tdSql.query("describe meters")
        tdSql.checkRows(8)
        tdSql.checkData(0, 1, "TIMESTAMP")
        tdSql.checkData(1, 1, "TINYINT")
        # 2.x is binary and 3.x is varchar
        # tdSql.checkData(2, 1, "BINARY")
        tdSql.checkData(2, 2, 23)
        tdSql.checkData(3, 1, "BOOL")
        tdSql.checkData(4, 1, "NCHAR")
        tdSql.checkData(4, 2, 29)
        tdSql.checkData(5, 1, "INT")
        # 2.x is binary and 3.x is varchar
        # tdSql.checkData(6, 1, "BINARY")
        tdSql.checkData(6, 2, 29)
        tdSql.checkData(6, 3, "TAG")
        tdSql.checkData(7, 1, "NCHAR")
        tdSql.checkData(7, 2, 31)
        tdSql.checkData(7, 3, "TAG")
        tdSql.query("show tables")
        tdSql.checkRows(2)
        tdSql.execute("drop database if exists newtest")

        cmd = "%s -t 2 -n 10 -b bool,tinyint,smallint,int,bigint,float,double,utinyint,usmallint,uint,ubigint,binary,nchar,timestamp -A bool,tinyint,smallint,int,bigint,float,double,utinyint,usmallint,uint,ubigint,binary,nchar,timestamp -y" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.query("show test.tables")
        tdSql.checkRows(2)
        tdSql.query("select count(*) from test.meters")
        tdSql.checkData(0, 0, 20)

        cmd = "%s -I stmt -t 2 -n 10 -b bool,tinyint,smallint,int,bigint,float,double,utinyint,usmallint,uint,ubigint,binary,nchar,timestamp -A bool,tinyint,smallint,int,bigint,float,double,utinyint,usmallint,uint,ubigint,binary,nchar,timestamp -y" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.query("show test.tables")
        tdSql.checkRows(2)
        tdSql.query("select count(*) from test.meters")
        tdSql.checkData(0, 0, 20)

        cmd = "%s -F 7 -n 10 -t 2 -y -M -I stmt" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.query("show test.tables")
        tdSql.checkRows(2)
        tdSql.query("select count(*) from test.meters")
        tdSql.checkData(0, 0, 20)

        cmd = "%s -n 3 -t 3 -B 2 -i 1 -G -y -T 1 2>&1 | grep sleep | wc -l" % binPath
        sleepTimes = subprocess.check_output(cmd, shell=True).decode("utf-8")
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        if int(sleepTimes) != 2:
            tdLog.exit("expected sleep times 2, actual %d" % int(sleepTimes))

        cmd = (
            "%s -n 3 -t 3 -B 2 -i 1 -G -y -T 1 -r 1 2>&1 | grep sleep | wc -l" % binPath
        )
        sleepTimes = subprocess.check_output(cmd, shell=True).decode("utf-8")
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        if int(sleepTimes) != 3:
            tdLog.exit("expected sleep times 3, actual %d" % int(sleepTimes))

        cmd = (
            "%s -n 3 -t 3 -B 2 -i 1 -G -y -T 1 -I sml 2>&1 | grep sleep | wc -l"
            % binPath
        )
        sleepTimes = subprocess.check_output(cmd, shell=True).decode("utf-8")
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        if int(sleepTimes) != 2:
            tdLog.exit("expected sleep times 2, actual %d" % int(sleepTimes))

        cmd = (
            "%s -n 3 -t 3 -B 2 -i 1 -G -y -T 1 -r 1 -I sml 2>&1 | grep sleep | wc -l"
            % binPath
        )
        sleepTimes = subprocess.check_output(cmd, shell=True).decode("utf-8")
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        if int(sleepTimes) != 3:
            tdLog.exit("expected sleep times 3, actual %d" % int(sleepTimes))

        cmd = (
            "%s -n 3 -t 3 -B 2 -i 1 -G -y -T 1 -I stmt 2>&1 | grep sleep | wc -l"
            % binPath
        )
        sleepTimes = subprocess.check_output(cmd, shell=True).decode("utf-8")
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        if int(sleepTimes) != 2:
            tdLog.exit("expected sleep times 2, actual %d" % int(sleepTimes))

        cmd = (
            "%s -n 3 -t 3 -B 2 -i 1 -G -y -T 1 -r 1 -I stmt 2>&1 | grep sleep | wc -l"
            % binPath
        )
        sleepTimes = subprocess.check_output(cmd, shell=True).decode("utf-8")
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        if int(sleepTimes) != 3:
            tdLog.exit("expected sleep times 3, actual %d" % int(sleepTimes))

        cmd = "%s -S 17 -n 3 -t 1 -y -x" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        time.sleep(2)  # to avoid invalid vgroup id
        tdSql.query("select last(ts) from test.meters")
        tdSql.checkData(0, 0, "2017-07-14 10:40:00.034")

        cmd = "%s -N -I taosc -t 11 -n 11 -y -x -E" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("use test")
        tdSql.query("show stables")
        tdSql.checkRows(0)
        tdSql.query("show tables")
        tdSql.checkRows(11)
        tdSql.query("select count(*) from `d10`")
        tdSql.checkData(0, 0, 11)

        cmd = "%s -N -I rest -t 11 -n 11 -y -x" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("use test")
        tdSql.query("show stables")
        tdSql.checkRows(0)
        tdSql.query("show tables")
        tdSql.checkRows(11)
        tdSql.query("select count(*) from d10")
        tdSql.checkData(0, 0, 11)

        cmd = "%s -N -I stmt -t 11 -n 11 -y -x" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("use test")
        tdSql.query("show stables")
        tdSql.checkRows(0)
        tdSql.query("show tables")
        tdSql.checkRows(11)
        tdSql.query("select count(*) from d10")
        tdSql.checkData(0, 0, 11)

        cmd = "%s -N -I sml -y" % binPath
        tdLog.info("%s" % cmd)
        assert os.system("%s" % cmd) != 0

        cmd = "%s -n 1 -t 1 -y -b bool" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("reset query cache")
        tdSql.query("describe test.meters")
        tdSql.checkData(1, 1, "BOOL")

        cmd = "%s -n 1 -t 1 -y -b tinyint" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("reset query cache")
        tdSql.query("describe test.meters")
        tdSql.checkData(1, 1, "TINYINT")

        cmd = "%s -n 1 -t 1 -y -b utinyint" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("reset query cache")
        tdSql.query("describe test.meters")
        tdSql.checkData(1, 1, "TINYINT UNSIGNED")

        cmd = "%s -n 1 -t 1 -y -b smallint" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("reset query cache")
        tdSql.query("describe test.meters")
        tdSql.checkData(1, 1, "SMALLINT")

        cmd = "%s -n 1 -t 1 -y -b usmallint" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("reset query cache")
        tdSql.query("describe test.meters")
        tdSql.checkData(1, 1, "SMALLINT UNSIGNED")

        cmd = "%s -n 1 -t 1 -y -b int" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("reset query cache")
        tdSql.query("describe test.meters")
        tdSql.checkData(1, 1, "INT")

        cmd = "%s -n 1 -t 1 -y -b uint" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("reset query cache")
        tdSql.query("describe test.meters")
        tdSql.checkData(1, 1, "INT UNSIGNED")

        cmd = "%s -n 1 -t 1 -y -b bigint" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("reset query cache")
        tdSql.query("describe test.meters")
        tdSql.checkData(1, 1, "BIGINT")

        cmd = "%s -n 1 -t 1 -y -b ubigint" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("reset query cache")
        tdSql.query("describe test.meters")
        tdSql.checkData(1, 1, "BIGINT UNSIGNED")

        cmd = "%s -n 1 -t 1 -y -b timestamp" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("reset query cache")
        tdSql.query("describe test.meters")
        tdSql.checkData(1, 1, "TIMESTAMP")

        cmd = "%s -n 1 -t 1 -y -b float" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("reset query cache")
        tdSql.query("describe test.meters")
        tdSql.checkData(1, 1, "FLOAT")

        cmd = "%s -n 1 -t 1 -y -b double" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("reset query cache")
        tdSql.query("describe test.meters")
        tdSql.checkData(1, 1, "DOUBLE")

        cmd = "%s -n 1 -t 1 -y -b nchar" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("reset query cache")
        tdSql.query("describe test.meters")
        tdSql.checkData(1, 1, "NCHAR")

        cmd = "%s -n 1 -t 1 -y -b nchar\(7\)" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("reset query cache")
        tdSql.query("describe test.meters")
        tdSql.checkData(1, 1, "NCHAR")

        # 2.x is binary and 3.x is varchar
        # cmd = "%s -n 1 -t 1 -y -b binary" %binPath
        # tdLog.info("%s" % cmd)
        # os.system("%s" % cmd)
        # tdSql.execute("reset query cache")
        # tdSql.query("describe test.meters")
        # tdSql.checkData(1, 1, "BINARY")

        # cmd = "%s -n 1 -t 1 -y -b binary\(7\)" %binPath
        # tdLog.info("%s" % cmd)
        # os.system("%s" % cmd)
        # tdSql.execute("reset query cache")
        # tdSql.query("describe test.meters")
        # tdSql.checkData(1, 1, "BINARY")

        cmd = "%s -n 1 -t 1 -y -A json\(7\)" % binPath
        tdLog.info("%s" % cmd)
        os.system("%s" % cmd)
        tdSql.execute("reset query cache")
        tdSql.query("describe test.meters")
        tdSql.checkData(4, 1, "JSON")

        cmd = "%s -n 1 -t 1 -y -b int,x" % binPath
        tdLog.info("%s" % cmd)
        assert os.system("%s" % cmd) != 0

        cmd = "%s -n 1 -t 1 -y -A int,json" % binPath
        tdLog.info("%s" % cmd)
        assert os.system("%s" % cmd) != 0

    def stop(self):
        tdSql.close()
        tdLog.success("%s successfully executed" % __file__)


tdCases.addWindows(__file__, TDTestCase())
tdCases.addLinux(__file__, TDTestCase())
