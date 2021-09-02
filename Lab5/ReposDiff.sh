git remote add -f tmp $1
git diff master remotes/tmp/master > reposDiff.diff
git remote rm tmp
