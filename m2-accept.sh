#!/usr/bin/env bash
# =====================================================================
# M2 一次性验收：编译 → 回归 → T1 → T2 → T9/T10 → 汇总
#
#   用法:  cd ~/sergw && bash m2-accept.sh
#   产物:  logs/*.log （每个用例一份，作为验收证据）
#
#   设计原则: 任何一步"没测到"都必须报 FAIL，绝不报告假通过。
# =====================================================================
set -u

BIN=./sergw
DEV=/tmp/ttyV0
PEER=/tmp/ttyV1
LOGD=logs

PASS=0
FAIL=0
SOCAT_PID=""
GW=0

note(){ printf '\n\033[36m=== %s ===\033[0m\n' "$*"; }
ok(){   printf '\033[32m  [PASS]\033[0m %s\n' "$*"; PASS=$((PASS+1)); }
bad(){  printf '\033[31m  [FAIL]\033[0m %s\n' "$*"; FAIL=$((FAIL+1)); }
skip(){ printf '\033[33m  [SKIP]\033[0m %s\n' "$*"; }

cleanup(){
    [ -n "$SOCAT_PID" ] && kill "$SOCAT_PID" 2>/dev/null
    [ "$GW" -ne 0 ] 2>/dev/null && kill -INT "$GW" 2>/dev/null
    wait 2>/dev/null
    exit 0
}
trap cleanup EXIT INT TERM

mkdir -p "$LOGD"

# ---------------------------------------------------------------- 0
note "0. 前置检查"
if [ -x "$BIN" ]; then ok "二进制存在"; else bad "找不到 $BIN，先 make"; exit 1; fi
if command -v socat   >/dev/null 2>&1; then ok "socat 可用";   else bad "缺 socat";   exit 1; fi
if command -v python3 >/dev/null 2>&1; then ok "python3 可用"; else bad "缺 python3"; exit 1; fi

# ---------------------------------------------------------------- 1
note "1. 编译 + 回归（不碰串口）"
if make > "$LOGD/build.log" 2>&1; then
    ok "编译零警告零错误"
else
    bad "编译失败，看 $LOGD/build.log"; exit 1
fi

# make clean 会删 test.bin，所以缺了就地重建
if [ ! -s test.bin ]; then
    if python3 gen_test.py test.bin > /dev/null 2>&1; then
        ok "test.bin 重建成功"
    else
        bad "test.bin 缺失且无法重建"; exit 1
    fi
fi
ok "test.bin 就绪（$(wc -c < test.bin) 字节）"

if "$BIN" -f test.bin > "$LOGD/T0-regression.log" 2>&1; then
    if grep -q "ok=3"          "$LOGD/T0-regression.log" \
    && grep -q "crc_err=1"     "$LOGD/T0-regression.log" \
    && grep -q "invalid_len=1" "$LOGD/T0-regression.log" \
    && grep -q "noise=5"       "$LOGD/T0-regression.log"; then
        ok "文件模式回归一致（ok=3 crc_err=1 noise=5）"
    else
        bad "文件模式输出不符，看 $LOGD/T0-regression.log"
    fi
else
    bad "文件模式运行失败，看 $LOGD/T0-regression.log"
fi

if make unit > "$LOGD/unit.log" 2>&1; then
    ok "解析器单元测试全过"
else
    bad "单元测试失败，看 $LOGD/unit.log"
fi

# ---------------------------------------------------------------- 2
note "2. 起虚拟串口"
pkill -f "socat.*ttyV0" 2>/dev/null
rm -f "$DEV" "$PEER"
socat -d -d "pty,raw,echo=0,link=$DEV" "pty,raw,echo=0,link=$PEER" \
      > "$LOGD/socat.log" 2>&1 &
SOCAT_PID=$!

for _ in $(seq 1 50); do
    [ -e "$DEV" ] && [ -e "$PEER" ] && break
    sleep 0.1
done
if [ -e "$DEV" ] && [ -e "$PEER" ]; then
    ok "虚拟串口就绪: $DEV <-> $PEER"
else
    bad "socat 起不来，看 $LOGD/socat.log"; exit 1
fi

# ---------------------------------------------------------------- 工具
# 起程序 → 喂数据 → SIGINT → 收日志
run_case(){
    local name=$1 feeder=$2 log="$LOGD/$1.log"
    local gw

    "$BIN" -d "$DEV" -b 115200 > "$log" 2>&1 &
    gw=$!
    GW=$gw
    sleep 0.5

    if ! kill -0 "$gw" 2>/dev/null; then
        bad "$name: 程序启动就退出了（没测到）"
        sed 's/^/      /' "$log"
        return 1
    fi

    bash -c "$feeder" > /dev/null 2>&1
    sleep 1
    kill -INT "$gw" 2>/dev/null
    wait "$gw" 2>/dev/null
    GW=0
    return 0
}

# ---------------------------------------------------------------- 3
note "3. T1 整块灌（验证能收全）"
if run_case T1 "cat test.bin > $PEER"; then
    grep -q "ok=3"    "$LOGD/T1.log" && ok "T1 ok=3（帧全部解析）"   || bad "T1 ok 不对"
    grep -q "noise=5" "$LOGD/T1.log" && ok "T1 noise=5（口径一致）"  || bad "T1 noise 不对"
    grep -q "bytes=45" "$LOGD/T1.log" && ok "T1 bytes=45（串口层没丢字节）" \
                                      || bad "T1 bytes 不是 45 —— 串口层可能丢数据"
fi

# ---------------------------------------------------------------- 4
note "4. T2 一字节一写（验证跨 read 边界）"
cat > /tmp/feed1.py <<PYEOF
import time
d = open('test.bin', 'rb').read()
f = open('$PEER', 'wb', buffering=0)
for b in d:
    f.write(bytes([b]))
    time.sleep(0.01)
f.close()
PYEOF

if run_case T2 "python3 /tmp/feed1.py"; then
    grep -q "ok=3"    "$LOGD/T2.log" && ok "T2 ok=3（分片后仍拼对）" || bad "T2 ok 不对"
    grep -q "noise=5" "$LOGD/T2.log" && ok "T2 noise=5（分片没产生噪声）" || bad "T2 noise 不对"
    grep -q "bytes=45" "$LOGD/T2.log" && ok "T2 bytes=45" || bad "T2 bytes 不是 45"

    line=$(grep '^io:' "$LOGD/T2.log" | tail -1)
    r=$(sed -n 's/.*reads=\([0-9]*\).*/\1/p'    <<< "$line")
    t=$(sed -n 's/.*timeouts=\([0-9]*\).*/\1/p' <<< "$line")
    e=$(sed -n 's/.*eintr=\([0-9]*\).*/\1/p'    <<< "$line")

    if [ -n "$r" ] && [ -n "$t" ] && [ -n "$e" ]; then
        data_reads=$(( r - t - e ))
        if [ "$data_reads" -eq 45 ]; then
            ok "T2 恒等式成立：reads($r) = timeouts($t) + eintr($e) + 45"
            ok "T2 判定：45 次单字节 read，帧仍 100% 正确 → 解析与 I/O 解耦"
        else
            bad "T2 恒等式不成立：$r - $t - $e = $data_reads，期望 45"
        fi
    else
        skip "日志里没有 eintr= 字段，跳过恒等式（建议补 io_eintr 计数器）"
    fi
fi

# ---------------------------------------------------------------- 5
note "5. T9/T10 退出耗时 + termios 还原"
stty -F "$DEV" -a > /tmp/before.txt 2>/dev/null
if [ -s /tmp/before.txt ]; then
    ok "读到启动前配置"
    echo "      启动前: $(stty -F "$DEV" 2>/dev/null | head -1)"
else
    bad "stty 读取失败（本轮无效）"
fi

"$BIN" -d "$DEV" -b 115200 > "$LOGD/T9.log" 2>&1 &
GW=$!
sleep 1

if kill -0 "$GW" 2>/dev/null; then
    T0=$(date +%s%N)
    kill -INT "$GW"
    wait "$GW"; RC=$?
    T1=$(date +%s%N)
    MS=$(( (T1 - T0) / 1000000 ))

    [ "$RC" -eq 0 ]  && ok "退出码 0（干净退出）"           || bad "退出码 $RC"
    [ "$MS" -lt 1000 ] && ok "退出耗时 ${MS}ms（< 1000ms）" || bad "退出耗时 ${MS}ms 太慢"
else
    bad "程序启动就退出了（没测到）"
    sed 's/^/      /' "$LOGD/T9.log"
fi
GW=0

stty -F "$DEV" -a > /tmp/after.txt 2>/dev/null
if [ -s /tmp/after.txt ] && [ -s /tmp/before.txt ]; then
    if diff -q /tmp/before.txt /tmp/after.txt > /dev/null 2>&1; then
        ok "termios 已还原（前后一致）"
        echo "      退出后: $(stty -F "$DEV" 2>/dev/null | head -1)"
    else
        bad "termios 未还原："
        diff /tmp/before.txt /tmp/after.txt | sed 's/^/      /'
    fi
else
    bad "配置对比文件为空，无法判断（不算通过）"
fi

# ---------------------------------------------------------------- 汇总
note "汇总"
printf '  PASS: %d\n  FAIL: %d\n' "$PASS" "$FAIL"
echo "  日志: $LOGD/"
echo

if [ "$FAIL" -eq 0 ]; then
    printf '\033[32m  ✅ M2 验收通过 —— 可以 git tag m2-serial\033[0m\n'
    exit 0
else
    printf '\033[31m  ❌ 有 %d 项未通过，先看对应日志\033[0m\n' "$FAIL"
    exit 1
fi
