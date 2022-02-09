/*
    _____           _____   _____   ____          ______  _____  ------
   |     |  |      |     | |     | |     |     | |       |            |
   |     |  |      |     | |     | |     |     | |       |            |
   | --- |  |      |     | |-----| |---- |     | |-----| |-----  ------
   |     |  |      |     | |     | |     |     |       | |       |
   | ____|  |_____ | ____| | ____| |     |_____|  _____| |_____  |_____


   Licensed under the MIT License <http://opensource.org/licenses/MIT>.

   Copyright © 2020-2022 Microsoft Corporation. All rights reserved.
   Author : <blobfusedev@microsoft.com>

   Permission is hereby granted, free of charge, to any person obtaining a copy
   of this software and associated documentation files (the "Software"), to deal
   in the Software without restriction, including without limitation the rights
   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
   copies of the Software, and to permit persons to whom the Software is
   furnished to do so, subject to the following conditions:

   The above copyright notice and this permission notice shall be included in all
   copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
   SOFTWARE
*/

package log

import (
	"blobfuse2/common"
	"log"
)

type SilentLogger struct {
}

func (*SilentLogger) GetLoggerObj() *log.Logger {
	return nil
}

func (*SilentLogger) GetType() string {
	return "silent"
}

func (*SilentLogger) GetLogLevel() common.LogLevel {
	return common.ELogLevel.LOG_OFF()
}

func (*SilentLogger) Debug(_ string, _ ...interface{}) {

}

func (*SilentLogger) Trace(_ string, _ ...interface{}) {

}

func (*SilentLogger) Info(_ string, _ ...interface{}) {

}

func (*SilentLogger) Warn(_ string, _ ...interface{}) {

}

func (*SilentLogger) Err(_ string, _ ...interface{}) {

}

func (*SilentLogger) Crit(_ string, _ ...interface{}) {

}

func (*SilentLogger) LogRotate() error {
	return nil
}

func (*SilentLogger) Destroy() error {
	return nil
}

func (*SilentLogger) SetLogFile(_ string) error {
	return nil
}

func (*SilentLogger) SetMaxLogSize(_ int) {
}

func (*SilentLogger) SetLogFileCount(_ int) {

}

func (*SilentLogger) SetLogLevel(_ common.LogLevel) {

}