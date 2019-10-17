pip install awscli
BINARY_NAME="${CI_REPO_NAME}_${CI_BRANCH}_${CI_COMMIT_ID:0:10}"
BINARY_NAME="${BINARY_NAME//Hireology\//}"
echo $BINARY_NAME
cp wal2json.so $BINARY_NAME
aws s3 cp "./${BINARY_NAME}" s3://hireology-artifacts/
