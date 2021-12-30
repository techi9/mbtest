#!/bin/bash

DEV="/dev/ttyS7"
BG="./modbus_bg device=${DEV} baudrate=115200"

if [ -e ${1} ]; then
  echo "Old address not set"
  exit 1
fi

if [ -e ${2} ]; then
  echo "New address not set"
  exit 1
fi

$BG set ${1} 0x300 0 48 32 > /dev/null
if [ $? -ne 0 ]; then
  $BG set ${1} 0x300 0 48 32
  exit 1
fi

OUTPUT=$($BG get ${1} 0x303 1 | tail -1)
CHA=$((OUTPUT >> 8))
CH_OLD_H=${1}
CH_OLD=$((CH_OLD_H))

if [ "${CH_OLD}" -ne "${CHA}" ]; then
  echo "Error: Address in EEPROM does not mapped."
  exit 1
fi

NEWVAL=$((OUTPUT & 255))
NEWVAL_H=${2}
NEWVAL_H=$((NEWVAL_H << 8))
NEWVAL=$((NEWVAL_H + NEWVAL))

$BG set ${1} 0x303 $NEWVAL

echo "Please restart module. New Address is ${2}"







