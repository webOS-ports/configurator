// Not built. Every rule in this directory must match something here;
// static-analysis.sh checks that, so a rule that stops matching (a
// Coccinelle upgrade, a typo) is noticed instead of passing quietly.
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string>

int createStamp(const std::string& stamp)
{
	return open(stamp.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
}

int createStampSafely(const std::string& stamp)
{
	return open(stamp.c_str(), O_CREAT | O_WRONLY | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
}

void scan(DIR* dp, const std::string& directory)
{
	struct dirent* dirp;
	struct stat buf;
	while ((dirp = readdir(dp)) != NULL) {
		std::string path = directory + "/" + dirp->d_name;
		if (stat(path.c_str(), &buf) != 0) {
			break;
		}
	}
}

void scanDeclared(DIR* dp, const std::string& directory)
{
	struct stat buf;
	while (const struct dirent* dirp = readdir(dp)) {
		const std::string path = directory + "/" + dirp->d_name;
		if (stat(path.c_str(), &buf) != 0)
			break;
	}
}
