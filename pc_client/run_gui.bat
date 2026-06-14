@echo off
setlocal
cd /d "%~dp0"
title ESP32 Hardware Wallet App

if not exist .venv (
  echo Creating Python virtual environment...
  py -m venv .venv 2>nul || python -m venv .venv
)

call .venv\Scripts\activate.bat

if not exist .venv\.deps_installed (
  echo Installing dependencies. This happens only once...
  python -m pip install --upgrade pip
  pip install -r requirements.txt
  echo ok> .venv\.deps_installed
)

python wallet_gui.py
endlocal
