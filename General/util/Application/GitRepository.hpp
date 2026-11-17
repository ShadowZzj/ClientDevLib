#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>
#include <General/ThirdParty/libgit2/build/include/git2.h>

namespace zzj
{
class GitRepository
{
   public:
    struct CommitInfo
    {
        std::string id;
        std::string author;
        std::string message;
    };

    static void InitializeLib()
    {
        if (++ref_count_ == 1)
        {
            git_libgit2_init();
            git_libgit2_opts(GIT_OPT_SET_OWNER_VALIDATION, 0);
        }
    }

    static void ShutdownLib()
    {
        if (--ref_count_ == 0)
        {
            git_libgit2_shutdown();
        }
    }

    GitRepository() { InitializeLib(); }
    void Open(const std::string& path)
    {
        if (repo_)
        {
            git_repository_free(repo_);
            repo_ = nullptr;
        }

        repo_ = OpenRepository(path);
    }
    ~GitRepository()
    {
        if (repo_) git_repository_free(repo_);
        ShutdownLib();
    }

    std::map<std::string, std::string> GetRemotes() const
    {
        git_strarray remote_list{nullptr, 0};
        CheckError(git_remote_list(&remote_list, repo_));

        std::map<std::string, std::string> remotes;
        for (size_t i = 0; i < remote_list.count; ++i)
        {
            const char* name = remote_list.strings[i];
            remotes[name] = GetRemoteUrl(name);
        }
        git_strarray_dispose(&remote_list);
        return remotes;
    }

    std::map<std::string, std::string> GetRemotesHttpsFormat() const
    {
        auto remotes = GetRemotes();
        std::map<std::string, std::string> https_remotes;
        for (const auto& [name, url] : remotes)
        {
            if (url.find("git@") == 0)
            {
                std::string https_url = url;
                https_url.replace(https_url.find(':'), 1, "/");
                https_url.replace(0, 4, "https://");
                https_url.erase(https_url.find(".git"));
                https_remotes[name] = https_url;
            }
            else
            {
                https_remotes[name] = url;
            }
        }
        return https_remotes;
    }
    CommitInfo GetHeadCommit() const
    {
        git_object* obj = nullptr;
        CheckError(git_revparse_single(&obj, repo_, "HEAD^{commit}"));

        const git_commit* commit = reinterpret_cast<git_commit*>(obj);
        CommitInfo info{OidToString(git_commit_id(commit)), git_commit_author(commit)->name,
                        git_commit_message(commit)};
        git_object_free(obj);
        return info;
    }

   private:
    static inline int ref_count_ = 0;
    git_repository* repo_ = nullptr;

    static git_repository* OpenRepository(const std::string& path)
    {
        git_repository* repo = nullptr;
        CheckError(git_repository_open(&repo, path.c_str()));
        return repo;
    }

    std::string GetRemoteUrl(const std::string& name) const
    {
        git_remote* remote = nullptr;
        CheckError(git_remote_lookup(&remote, repo_, name.c_str()));

        const char* url = git_remote_url(remote);
        std::string url_str = url ? url : "";
        git_remote_free(remote);
        return url_str;
    }

    static std::string OidToString(const git_oid* oid)
    {
        char buf[512];
        git_oid_tostr(buf, sizeof(buf), oid);
        return std::string(buf);
    }

    // 统一错误处理
    static void CheckError(int error_code)
    {
        if (error_code >= 0) return;

        const git_error* e = git_error_last();
        std::string msg = e ? e->message : "Unknown git error";
        throw std::runtime_error("Git error: " + msg);
    }
};
};  // namespace zzj