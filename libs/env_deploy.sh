SRC_ROOT="$PWD"
DEPLOYMENT="$SRC_ROOT/deployment"
BUILD="$SRC_ROOT/build"
# Use RELEASE_TAG from CI if set, otherwise read from file
if [ -n "$RELEASE_TAG" ]; then
  version_standalone="$RELEASE_TAG"
else
  version_standalone=$(cat nekoray_version.txt)
fi
