#include <iostream>
#include <vector>
extern "C" {
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <libssh2.h>
#include <libssh2_sftp.h>
#include <libavutil/mem.h>
#include <netdb.h>
#include <archive.h>
#include <assert.h>
#include <archive_entry.h>

static void *my_mal(size_t s, void **d)
{
    (void) d;
    return malloc(s);
}

static void my_fr(void *c, void **d)
{
    (void) d;
    free(c);
}

static void *my_rel(void *d, size_t s, void **c)
{
    return realloc(d, s);
}
};

using namespace std;
struct _Path : string
{
	_Path (string s) : string(s){}
};

struct Ssh {

	Ssh(string usr,
	string passwd,
	string host,
	string port="21")
	{
		ses = libssh2_session_init_ex(my_mal, my_fr, my_rel, 0);
		try {
		    struct addrinfo hi;
		    struct addrinfo *p, *r;

		    memset(&hi, 0, sizeof(hi));

		    hi.ai_family = AF_UNSPEC;
		    hi.ai_socktype = SOCK_STREAM;

		    int s;
		    int sfd;
		    do {
			s = getaddrinfo(host.c_str (),
					port.c_str (), &hi, &r);
			if (s)
				throw s;
			p = r;

			while (p) {
			    sfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
			    if (sfd != -1) {
				if (-1 != connect(sfd, p->ai_addr, p->ai_addrlen))
					break;
				close(sfd);
			    }
			    p = p->ai_next;
			}

			if (!p) {
			    freeaddrinfo(r);
			    throw -1;
			}
			freeaddrinfo(r);

			libssh2_session_startup(ses, sfd);

			s = libssh2_userauth_password(ses, usr.c_str (),
					passwd.c_str ());
			if (s)
			    throw -1;

			fp = libssh2_sftp_init(ses);
			if (!fp)
				throw -1;
		    } while (0);
		}catch (...)
		{
			libssh2_session_free(ses);
			throw;
		}
	}
	LIBSSH2_SFTP_ATTRIBUTES stat (string file)
	{
		LIBSSH2_SFTP_ATTRIBUTES st;
		if (libssh2_sftp_stat (fp, file.c_str (),
				&st))
			throw -1;
		return st;
	}

	vector <_Path> ls (string dir)
	{
		ffp=libssh2_sftp_opendir (fp, dir.c_str ());
		if (!ffp) throw -1;

		vector <_Path> vc;
		char buf[1054];
		while (libssh2_sftp_readdir(ffp, buf, 1054, 0))
		{

			vc.push_back (string (buf));
		}
		libssh2_sftp_close(ffp);
		return vc;
	}
	void cat (string file, struct archive *arch_ptr)
	{
		    do {
			ffp = libssh2_sftp_open(fp, file.c_str (),
					0, LIBSSH2_FXF_READ);

			do {
			    if (!ffp)
				break;
			    char buf[1054];

			    libssh2_sftp_seek(ffp, 0);
			    while (1) {
				int r = libssh2_sftp_read(ffp, buf, sizeof(buf));
				if (!r)
				    break;
				archive_write_data(arch_ptr,
				buf, r);
			    }
			} while (0);
			libssh2_sftp_close(ffp);
		    } while (0);
	}
	~Ssh ()
	{
		libssh2_sftp_shutdown(fp);
		libssh2_session_free(ses);
	}

	LIBSSH2_SESSION *ses;
	LIBSSH2_SFTP *fp;
	LIBSSH2_SFTP_HANDLE *ffp;
};

void
ssh_rec (Ssh& _comp, string path, void *dptr)
try{
	    for(string& str: _comp.ls (path))
	    {
		    if (!strncmp ("..", str.c_str (), str.length()))
			    continue;
		    string pa(string (path)+string ("/")+str);

		    LIBSSH2_SFTP_ATTRIBUTES attr = _comp.stat (pa);
		    if (attr.permissions & LIBSSH2_SFTP_S_IFDIR)
		    {
			    ssh_rec (_comp, pa, dptr);
		    }else if (attr.permissions & LIBSSH2_SFTP_S_IFREG)
		    {
			    struct archive_entry *ent=archive_entry_new ();

			    archive_entry_set_pathname (ent, pa.c_str ());
			    archive_entry_set_size (ent, attr.filesize);
			    archive_entry_set_uid (ent, attr.uid);
			    archive_entry_set_gid (ent, attr.gid);
			    archive_entry_set_mode (ent, S_IFREG);
			    archive_entry_set_atime (ent, attr.atime, 0);
			    archive_entry_set_mtime (ent, attr.mtime, 0);

			    assert (archive_write_header ((struct archive*)dptr, 
					ent)==0);

			    _comp.cat (pa, (struct archive*)dptr);
			    archive_entry_free (ent);
		    }
	    }
}catch(...){};

int main(
int argsc,
char **args,char **env)
{
    libssh2_init(0);
    struct archive *arch=archive_write_new ();

    archive_write_set_format_gnutar(arch);

    archive_write_open_FILE (arch, stdout);

    try {
	    Ssh sh("edd", "node", "localhost", "9000");
	    ssh_rec (sh, string (args[1]), arch);
    } catch (int &i)
    {
    }
    archive_write_close (arch);

    libssh2_exit();
    return 0;
}
