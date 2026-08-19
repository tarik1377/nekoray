SRC_ROOT="$PWD"
DEPLOYMENT="$SRC_ROOT/deployment"
BUILD="$SRC_ROOT/build"
# Use RELEASE_TAG from CI if set, otherwise read from file
if [ -n "$RELEASE_TAG" ]; then
  version_standalone="$RELEASE_TAG"
else
  version_standalone=$(cat nekoray_version.txt)
fi

# Tolerate padding around a pasted tag, but never whitespace inside it. The version
# ends up in -ldflags, which the Go tool splits on spaces: a tag like
# "GreenRhythm v1.4.3" turned "v1.4.3" into a stray argument for the linker, which
# answered with its own usage text and failed the release build 40s in, with
# nothing in the log pointing at the tag. Fail here instead, while we can say why.
version_standalone=$(printf '%s' "$version_standalone" | tr -d '\r\n' | sed 's/^[[:space:]]*//; s/[[:space:]]*$//')
case "$version_standalone" in
"")
  echo "version is empty: set RELEASE_TAG or fill nekoray_version.txt" >&2
  exit 1
  ;;
*[[:space:]]*)
  echo "version must not contain whitespace, got: '$version_standalone'" >&2
  echo "use a plain tag such as v1.4.3 (git tags cannot contain spaces either)" >&2
  exit 1
  ;;
esac
