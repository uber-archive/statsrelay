#!/bin/bash

VERSION=$1

if [ -z "$VERSION" ]; then
	echo "Usage: ./tag-release.sh <version>"
	exit 1
fi

echo "Tagging master as $VERSION"
git checkout master
echo "$VERSION" > VERSION
git commit -m "Release $VERSION" VERSION
git tag upstream/$VERSION

for distr in precise squeeze ; do
    git checkout debian-$distr
    git merge upstream/$VERSION
    debchange -v $VERSION-1 -D unstable "New upstream release"
    git commit -m "Updated changelog for $VERSION-1" debian/changelog
    git tag debian-$distr/$VERSION-1
done

echo -n "Tagging complete. Are you sure you want to push to origin (yes/no)? "
read CONFIRM
if [ "$CONFIRM" != "yes" ]; then
	echo "Abort! You need to reset to origin and delete tags or push to origin manually now"
	exit 2
fi

for distr in precise squeeze ; do
    git push origin debian-$distr
done
git checkout master
git push origin master
git push origin --tags
echo "$VERSION released!"
