if [ "$(basename "$PWD")" = "error-analysis" ]; then
    cd ..
fi

./cmake-build-release/simulation-orbit --dt 100 --years 200 --headless --j2 | python3 analyze.py analyze
./cmake-build-release/simulation-orbit --dt 300 --years 200 --headless      | python3 analyze.py analyze
./cmake-build-release/simulation-orbit --dt 300 --years 200 --headless --j2 | python3 analyze.py analyze
./cmake-build-release/simulation-orbit --dt 900 --years 200 --headless      | python3 analyze.py analyze
./cmake-build-release/simulation-orbit --dt 900 --years 200 --headless --j2 | python3 analyze.py analyze
