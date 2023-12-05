@echo off

IF "%1"=="" (
    echo Error: Missing update type argument (must be one of patch, minor, major, prepatch, preminor, premajor, prerelease^)
    exit /B 1
)

echo Updating version
CALL npm version -m "Update version to %%%%s" "%1" || goto :eof

echo Publishing...
CALL npm publish || goto :eof

echo Pushing commit and tag
CALL git push || goto :eof
CALL git push --tags || goto :eof

echo Finished