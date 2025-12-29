@echo off
set VENV_DIR=.venv

IF NOT EXIST %VENV_DIR% (
    echo "Creating virtual environment..."
    python -m venv %VENV_DIR%
)

echo "Activating virtual environment..."
call %VENV_DIR%\Scripts\activate.bat

echo "Checking for requirement updates..."
pip install -r requirements.txt

echo "Starting the application..."
start http://127.0.0.1:5000
python app.py