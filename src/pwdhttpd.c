/*
 * $Id$
 *
 * pwdhttpd
 * A small web server that serves the current directory
 *
 * Written by Mina Naguib
 * http://mina.naguib.ca
 *
 * License: GPL
 */

#define LISTENQ 1024
#define BUFFLEN 65536
#define CSS "<style type=\"text/css\"><!-- body { background-color: #D7D7B7; color: #7A1F1F; margin: 0px; padding: 0px; font-family: sans-serif;} a, :link { color: #804747; } a:hover, :link:hover { background-color: #C7C7A7; color: #AA3F3F; } h1 { margin: 0px; padding: 10px 3px 5px 3px; } #content { background-color: #EDEDDE; border: 1px solid #C1C18F; border-right-width: 5px; border-bottom-width: 5px; margin: 10px; padding: 15px;} #footer { padding: 3px 15px 0px 0px; font-size: x-small; text-align: right; } --></style>"
#define SIGNATURE "<div id=\"footer\"><p><i><a href=\"http://mina.naguib.ca/?pwdhttpd\">" PACKAGE_STRING "</a></i></p></div>"
#include <config.h>

#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <event.h>
#include <sys/uio.h>
#include <unistd.h>
#include <ctype.h>
#include <signal.h>
#include <sys/param.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#ifdef HAVE_DNS_SD_H
#include <dns_sd.h>
#endif

/*
 * Custom types
 */

/* Is the connection talking short or long (headers follow) http */
typedef enum _httpmode_t {
	HTTPMODE_SHORT,
	HTTPMODE_LONG
} httpmode_t;

/* What status can a connection be at ? */
typedef enum _status_t {
	STATUS_CONNECTED,
	STATUS_READINGREQUEST,
	STATUS_SENDINGCONTENT,
	STATUS_CLOSING
} status_t;

/*
 * All the data for a single client connection
 */
typedef struct _connection_t {
	int fd;
	struct sockaddr_in sa;
	struct bufferevent * be;
	char * ip;
	int port;
	httpmode_t httpmode;
	status_t status;
	char * request;
	int filefd;
	int sentheaders;
	time_t connected;
	struct _connection_t * next;
} connection_t;

/*
 * Dirty hack - only common mime types
 */
char * mimetypes[][2] = {
	{".asc" , "text/plain"},
	{".asf" , "video/x-ms-asf"},
	{".asx" , "video/x-ms-asf"},
	{".avi" , "video/x-msvideo"},
	{".bz2" , "application/x-bzip"},
	{".c" , "text/plain"},
	{".class" , "application/octet-stream"},
	{".conf" , "text/plain"},
	{".cpp" , "text/plain"},
	{".css" , "text/css"},
	{".dtd" , "text/xml"},
	{".dvi" , "application/x-dvi"},
	{".gif" , "image/gif"},
	{".gz" , "application/x-gzip"},
	{".htm" , "text/html"},
	{".html" , "text/html"},
	{".jpeg" , "image/jpeg"},
	{".jpg" , "image/jpeg"},
	{".js" , "text/javascript"},
	{".log" , "text/plain"},
	{".m3u" , "audio/x-mpegurl"},
	{".m4p" , "audio/mp4a-latm"},
	{".mov" , "video/quicktime"},
	{".mp3" , "audio/mpeg"},
	{".mpeg" , "video/mpeg"},
	{".mpg" , "video/mpeg"},
	{".ogg" , "application/ogg"},
	{".pac" , "application/x-ns-proxy-autoconfig"},
	{".pdf" , "application/pdf"},
	{".pl" , "text/plain"},
	{".png" , "image/png"},
	{".ps" , "application/postscript"},
	{".qt" , "video/quicktime"},
	{".sig" , "application/pgp-signature"},
	{".spl" , "application/futuresplash"},
	{".swf" , "application/x-shockwave-flash"},
	{".tar" , "application/x-tar"},
	{".tar.bz2" , "application/x-bzip-compressed-tar"},
	{".tar.gz" , "application/x-tgz"},
	{".tbz" , "application/x-bzip-compressed-tar"},
	{".text" , "text/plain"},
	{".tgz" , "application/x-tgz"},
	{".torrent" , "application/x-bittorrent"},
	{".txt" , "text/plain"},
	{".wav" , "audio/x-wav"},
	{".wax" , "audio/x-ms-wax"},
	{".wma" , "audio/x-ms-wma"},
	{".wmv" , "video/x-ms-wmv"},
	{".xbm" , "image/x-xbitmap"},
	{".xml" , "text/xml"},
	{".xpm" , "image/x-xpixmap"},
	{".xwd" , "image/x-xwindowdump"},
	{".zip" , "application/zip"},
	{NULL, NULL}
};

/*
 * Globals are evil, mkay ?
 */
unsigned short port = 0;
connection_t * connectionlist = NULL;
unsigned int numconnections = 0;

/*
 * Takes a string
 * Returns a new string with HTML entities escaped
 * free() result when done with it
 */
char * htmlencode (char * str) {
	unsigned char c = 0;
	int i, l = 0;
	char tempbuf[10];
	char *res = NULL;
	char *sp = NULL;

	l = strlen(str);

	res = malloc((l*6)+1);
	memset(res, 0, (l*6)+1);
	sp = res;

	for (i=0; i<l; i++) {

		c = str[i];
		if (strchr("<>\"'\\&", c)) {
			/* Let's encode it */
			snprintf(tempbuf, 10, "&#%u;", c);
			strcat(sp, tempbuf);
			sp += strlen(tempbuf);
		}
		else {
			/* Keep as-is */
			*sp = c;
			sp++;
		}

	}

	return res;
}


/*
 * Takes an opened directory
 * Returns an html string of the list of files in it
 * free() result when done with it
 */
char * htmldir(DIR * dir) {
	char * html = NULL;
	char * temp = NULL;
	char * name = NULL;;
	char * encodedname = NULL;;
	struct dirent * entry = NULL;

	asprintf(&html, "<a href=\"../\">[Parent Directory]</a>\n<ul>");

	while ((entry = readdir(dir))) {
		if (entry->d_name[0] != '.') {
			if (DT_DIR & entry->d_type) {
				asprintf(&name, "%s/", entry->d_name);
			}
			else {
				name = strdup(entry->d_name);
			}
			encodedname = htmlencode(name);

			temp = html;
			asprintf(&html, "%s<li> <a href=\"%s\">%s</a></li>\n", temp, encodedname, encodedname);
			free(temp);

			free(encodedname);
			free(name);
		}
	}

	temp = html;
	asprintf(&html, "%s</ul>\n", temp);
	free(temp);

	return html;
}

/*
 * Takes a URL-escaped string
 * Returns the decoded string
 * free() result when done with it
 */
char * urldecode (char * str) {
	unsigned char c, c1, c2 = 0;
	int i, l = 0;
	char hex[3];
	char *res = NULL;
	char *sp = NULL;

	l = strlen(str);

	res = malloc(l+1);
	memset(res, 0, l+1);
	sp = res;

	for (i=0; i<l; i++) {

		c = str[i];

		if (c == '%' && ((l - i) > 2)) {
			/* Possible Hex-encoded stuff */
			c1 = str[i+1];
			c2 = str[i+2];
			if (((c1 >= '0' && c1 <= '9') || (c1 >= 'A' && c1 <= 'F')) && ((c2 >= '0' && c2 <= '9') || (c2 >= 'A' && c2 <= 'F'))) {
				/* Yes it is - change c and advance i */
				sprintf(hex, "%c%c", c1, c2);
				c = strtol(hex, NULL, 16);
				i+=2;
			}
		}

		/* Append c to result */
		*(sp++) = c;
	}

	return res;
}

/*
 * Takes a requested resource string (should be already urldecode()d)
 * Returns the matched path to serve as string
 * free() result when done with it
 */
char * request2path (char * request) {
	char * newrequest = NULL;
	char resolved[PATH_MAX] = {0};
	int resolvedlen = 0;
	char * path = NULL;
	char * cwd = NULL;
	char resolvedcwd[PATH_MAX] = {0};
	int resolvedcwdlen = 0;

	/*
	 * Set newrequest
	 */
	if  (*request == '/')
		asprintf(&newrequest, ".%s", request);
	else
		newrequest = strdup(request);

	/*
	 * Set resolvedcwd & resolvedcwdlen
	 */
	cwd = getcwd(NULL, 0);
	realpath(cwd, resolvedcwd);
	free(cwd);
	resolvedcwdlen = strlen(resolvedcwd);

	/*
	 * Set resolved and resolvedlen
	 */
	realpath(newrequest, resolved);
	resolvedlen = strlen(resolved);

	free(newrequest);

	if (
			resolvedcwdlen && resolvedlen
			&&
			strstr(resolved, resolvedcwd) == resolved
			&&
			(
			 resolvedcwdlen == resolvedlen
			 ||
			 *(resolved+resolvedcwdlen-1) == '/'
			 ||
			 *(resolved+resolvedcwdlen) == '/'
			)
		) {
		path = strdup(resolved);
	}

	return (path);
}

/*
 * SIGPIPE's non fatal
 */
void sig_pipe(int i) {
}

/*
 * Takes a string
 * Returns true if it's printable, false if not
 */
int isallprint (char * s) {
	int l = strlen(s);
	int i;
	for (i=0; i<l; i++) {
		if (!isprint(s[i]))
			return 0;
	}
	return 1;
}

/*
 * Takes a file descriptor
 * Sets it to non-block
 */
int setnonblock(int fd) {
	int flags;

	flags = fcntl(fd, F_GETFL);
	if (flags < 0)
		return flags;
	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0)
		return -1;

	return 0;
}

/*
 * Takes a path (hopefully a file)
 * Returns a string of it's mime-type
 */
char * mimetype (char * path) {
	char * temp = path;
	char * ext = NULL;
	char * mimetype = NULL;
	char * (*pair)[2];
	int i, j;
	int fd;
	int ascii_count = 0, binary_count = 0;
	unsigned char buffer[1024] = {0};

	/*
	 * Get extension
	 */
	if ((ext = strrchr(temp, '.'))) {
		/*
		 *  Got it - normalize it
		 */
		ext = strdup(ext);
		for (i=0; ext[i]; i++)
			ext[i] = tolower(ext[i]);

		/*
		 * Then try to find the mimetype for it
		 */
		for (pair = mimetypes; (*pair)[0]; pair++) {
			i = strcmp((*pair)[0], ext);
			if (i == 0) {
				/* Exact match */
				mimetype = (*pair)[1];
				break;
			}
			else if (i > 0) {
				/* mimeetypes is sorted and we're past where we could match */
				break;
			}
		}

		free(ext);
	}

	if (!mimetype) {
		/*
		 * No mimetype found via extension
		 * Assume binary
		 */
		if ((fd = open(path, O_RDONLY))) {
			/*
			 * But let's see if we can see if the first 1k of file is all ascii
			 */
			if ((i = read(fd, buffer, 1024))) {
				for (j = 0; j<i; j++) {
					if (isprint(buffer[j]) || isspace(buffer[j]))
						ascii_count++;
					else
						binary_count++;
				}
			}
			close(fd);
		}

		if (ascii_count >= (binary_count * 10))
			mimetype = "text/plain";
		else
			mimetype = "application/octet-stream";
	}

	return (mimetype);
}

/*
 * Takes a connection
 * Destroyes it, removes it from connectionlist, closes and free() it and it's properties
 */
void connection_destroy(connection_t * connection) {
	connection_t * prev = NULL;
	connection_t * x = NULL;

	/*
	 *  Populate prev
	 */
	for (x = connectionlist; x; x = x->next) {
		if (x->next == connection) {
			prev = x;
			break;
		}
	}

	/*
	 * Delete connection from connectionlist reference-wise
	 */
	if (prev) {
		prev->next = connection->next;
		numconnections--;
	}
	else if (connectionlist == connection) {
		connectionlist = connection->next;
		numconnections--;
	}

	fprintf(stderr, "Closing connection with [%s:%u]. [%u] connection(s) left.\n", connection->ip, connection->port, numconnections);

	/*
	 * Cleanup internals & free
	 */
	if (connection->fd > 0) {
		close(connection->fd);
		connection->fd = 0;
	}
	if (connection->be) {
		bufferevent_free(connection->be);
		connection->be = NULL;
	}
	if (connection->ip) {
		free(connection->ip);
	}
	if (connection->request) {
		free(connection->request);
	}
	if (connection->filefd > 0) {
		close(connection->filefd);
		connection->filefd = 0;
	}
	free(connection);
}

/*
 * Takes a connection
 * Adds it to the connectionlist
 */
void connectionlist_add(connection_t * connection) {
	connection_t * x = NULL;

	for (x=connectionlist; x && x->next; x = x->next);
	if (x) {
		x->next = connection;
	}
	else {
		connectionlist = connection;
	}

	connection->connected = time(NULL);
	numconnections++;

	fprintf(stderr, "Accepted new connection from [%s:%u]. [%u] connection(s) connected\n", connection->ip, connection->port, numconnections);
}

/*
 * Takes a connection
 * Sends it content (magic)
 * Then destroys the connection when there's nothing more to send
 */
void connection_sendcontent(connection_t * connection) {
	char * content = NULL;
	int freecontent = 0;
	char * temp = NULL;
	int contentlength = 0;
	int fd;
	char *path = NULL;
	char *newpath = NULL;
	int path_isfile = 0, path_isdir = 0;
	off_t path_size = 0;
	char buffer[BUFFLEN];
	struct stat sb;
	DIR * dir = NULL;

	if (connection->status == STATUS_CONNECTED) {
		/* Ignore it */
		return;
	}
	else if (connection->status != STATUS_SENDINGCONTENT) {
		connection_destroy(connection);
		return;
	}

	if (!connection->sentheaders) {
		/*
		 * Initial call
		 *
		 * Decide on what we'll send
		 */

		path = request2path(connection->request);

		if (path) {
			if (stat(path, &sb) == -1) {
				/* Does not exist */
			}
			else if (S_IFREG & sb.st_mode) {
				path_isfile = 1;
				path_size = sb.st_size;
			}
			else if (S_IFDIR & sb.st_mode) {
				path_isdir = 1;
				/*
				 * However is there an index ?
				 */
				asprintf(&newpath, "%s/index.html", path);
				if (stat(newpath, &sb) != -1 && S_IFREG & sb.st_mode) {
					/* Yes */
					path_isdir = 0;
					path_isfile = 1;
					path_size = sb.st_size;
					free(path);
					path = newpath;
				}
				else {
					/* No */
					free(newpath);
				}
			}
		}


		if (path_isdir) {
			/*
			 * Directory listing
			 */

			if ((dir = opendir(path))) {
				fprintf(stderr, "Serving directory: [%s]\n", path);

				temp = htmldir(dir);
				asprintf(&content,
						"HTTP/1.0 200 OK Directory listing follows\r\n"
						"Connection: close\r\n"
						"Server: " PACKAGE_STRING "\r\n"
						"Content-Type: text/html; charset=UTF-8\r\n"
						"\r\n"
						"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\r\n"
						"<html><head>"
						"<title>Index of %s</title>"
						CSS
						"</head><body>"
						"<h1>Index of %s</h1>"
						"<div id=\"content\">%s</div>"
						SIGNATURE
						"</body></html>"
						, connection->request, connection->request, temp);
				free(temp);
				freecontent = 1;

				closedir(dir);
			}
			else {
				fprintf(stderr, "Serving error since I cannot open directory: [%s]\n", path);
				content =
					"HTTP/1.0 500 Houston we have a problem\r\n"
					"Connection: close\r\n"
					"Server: " PACKAGE_STRING "\r\n"
					"Content-Type: text/html; charset=UTF-8\r\n"
					"\r\n"
					"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\r\n"
					"<html><head>"
					"<title>500 - Internal server error</title>"
					CSS
					"</head><body>"
					"<h1>500 - Internal server error</h1>"
					"<div id=\"content\"><p>Please try again later.</p></div>"
					SIGNATURE
					"</body></html>"
					;
			}
			contentlength = strlen(content);
		}
		else if (path_isfile) {
			/*
			 * File contents
			 */
			if ((fd = open(path, O_RDONLY, 0))) {
				fprintf(stderr, "Serving file: [%s]\n", path);
				connection->filefd = fd;
				asprintf(&content,
						"HTTP/1.0 200 OK File contents follows\r\n"
						"Connection: close\r\n"
						"Server: " PACKAGE_STRING "\r\n"
						"Content-Type: %s; charset=UTF-8\r\n"
						"Content-Length: %llu\r\n"
						"\r\n"
						, mimetype(path), path_size
						);
				freecontent = 1;
			}
			else {
				fprintf(stderr, "Serving error since I cannot open: [%s]\n", path);
				content =
					"HTTP/1.0 500 Houston we have a problem\r\n"
					"Connection: close\r\n"
					"Server: " PACKAGE_STRING "\r\n"
					"Content-Type: text/html; charset=UTF-8\r\n"
					"\r\n"
					"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\r\n"
					"<html><head>"
					"<title>500 - Internal server error</title>"
					CSS
					"</head><body>"
					"<h1>500 - Internal server error</h1>"
					"<div id=\"content\"><p>Please try again later.</p></div>"
					SIGNATURE
					"</body></html>"
					;
			}
			contentlength=strlen(content);
		}
		else {
			/*
			 * 404
			 */

			fprintf(stderr, "Serving 404 since I cannot find: [%s]\n", path);

			content =
				"HTTP/1.0 404 I call your bluff\r\n"
				"Connection: close\r\n"
				"Server: " PACKAGE_STRING "\r\n"
				"Content-Type: text/html; charset=UTF-8\r\n"
				"\r\n"
				"<!DOCTYPE html PUBLIC \"-//W3C//DTD XHTML 1.0 Strict//EN\" \"http://www.w3.org/TR/xhtml1/DTD/xhtml1-strict.dtd\">\r\n"
				"<html><head>"
				"<title>404 - Not Found</title>"
				CSS
				"</head><body>"
				"<h1>404 - Not Found</h1>"
				"<div id=\"content\"><p>The file you have requested does not exist.</p></div>"
				SIGNATURE
				"</body></html>"
				;
			contentlength = strlen(content);
		}

		if (path)
			free(path);

		connection->sentheaders = 1;

	}
	else if (connection->filefd) {
		/*
		 * Send next chunk from file
		 */
		if ((contentlength = read(connection->filefd, buffer, BUFFLEN)) > 0) {
			content = buffer;
		}
	}


	if (content) {
		bufferevent_write(connection->be, content, contentlength);
		if (freecontent)
			free(content);
	}


	if (EVBUFFER_LENGTH(connection->be->output) < 1) {
		/* Nothing more to send - prepare closure past next write confirmation*/
		connection_destroy(connection);
	}

}

/*
 * Lib Event callback: Connection has data to read
 */
void cb_connectionread(struct bufferevent *be, void * connection) {
	char * buffer = NULL;
	int bufferlen = 0;
	char * request = NULL;
	char * requestend = NULL;
	status_t status = ((connection_t *)connection)->status;
	char * temp;

	while ((buffer = evbuffer_readline(be->input))) {
		bufferlen = strlen(buffer);

		/*
			if (isallprint(buffer)) {
			fprintf(stderr, "Read from [%s:%u]: [%s]\n", ((connection_t *)connection)->ip, ((connection_t *)connection)->port, buffer);
			}
			*/

		if (status == STATUS_CONNECTED) {
			/* Initial request line */
			if (bufferlen < 5 || strstr(buffer, "GET ") != buffer) {
				/* Cannot support this garbage */
				connection_destroy(connection);
			}
			else {

				/*
				 * Prepare request
				 */
				request = buffer + 4;
				while (*request == ' ') request++;
				requestend = strchr(request, ' ');
				while (requestend && *requestend == ' ') {
					*requestend = '\0';
					requestend++;
				}
				request = urldecode(request);
				if ((temp = strchr(request, '?')))
					*temp = '\0';

				if (request && strlen(request) > 0) {

					/*
					 * Seems like a valid request
					 */
					((connection_t *)connection)->request = request;

					if (requestend && strstr(requestend, "HTTP/") == requestend) {
						/* Sounds like a long HTTP request */
						((connection_t *)connection)->httpmode = HTTPMODE_LONG;
						((connection_t *)connection)->status = status = STATUS_READINGREQUEST;
					}
					else {
						/* Sounds like an short HTTP request */
						((connection_t *)connection)->httpmode = HTTPMODE_SHORT;
						((connection_t *)connection)->status = status = STATUS_SENDINGCONTENT;
						connection_sendcontent(connection);
					}

				}
				else {
					/*
					 * Invalid request
					 */
					if (request)
						free(request);
					connection_destroy(connection);
				}

			}

		}
		else if (status == STATUS_READINGREQUEST && ((connection_t *)connection)->httpmode == HTTPMODE_LONG) {
			/* We don't actually care about the headers.. just wait for empty line */
			if (bufferlen == 0) {
				/* There it is */
				((connection_t *)connection)->status = STATUS_SENDINGCONTENT;
				connection_sendcontent(connection);
			}
		}
		else if (status == STATUS_SENDINGCONTENT) {
			/* Ignore data */
		}
		else {
			connection_destroy(connection);
		}

		free(buffer);
	}

}

/*
 * Lib Event callback: Just wrote something to a connection
 */
void cb_connectionwrite(struct bufferevent *be, void * connection) {
	/* Send more content */
	connection_sendcontent(connection);

}

/*
 * Lib Event Callback: Error/Disconnection
 */
void cb_connectionerror(struct bufferevent *be, short what, void * connection) {
	connection_destroy(connection);
}

/*
 * Lib Event Callback on parent listener = new connection
 */
void cb_newconnection(int server_fd, short event, void * serve_ev) {
	connection_t * connection = NULL;
	socklen_t calen;

	/*
	 * Prepare connection struct
	 */
	if (!(connection = malloc(sizeof(connection_t)))) {
		perror("Error allocating new connection");
		return;
	}
	bzero(connection, sizeof(connection_t));

	/*
	 * Accept connection
	 */
	calen = sizeof(connection->sa);
	if ((connection->fd = accept(server_fd, (struct sockaddr *)&(connection->sa), &calen)) == -1) {
		perror("Error accepting new connection");
		connection_destroy(connection);
		return;
	}

	/* Init */
	asprintf(&(connection->ip), "%s", inet_ntoa(connection->sa.sin_addr));
	connection->port = ntohs(connection->sa.sin_port);
	setnonblock(connection->fd);

	/*
	 * Setup the bufferevent
	 */
	if (!(connection->be = bufferevent_new(connection->fd, &cb_connectionread, &cb_connectionwrite, &cb_connectionerror, connection))) {
		fprintf(stderr, "Error setting up bufferevent\n");
		connection_destroy(connection);
		return;
	}
	bufferevent_enable(connection->be, EV_READ | EV_WRITE);
	event_priority_set(&connection->be->ev_read, 2);

	/*
	 * Append to connectionlist
	 */
	connectionlist_add(connection);

}

/*
 * Program starts running here
 */
int main(int argc, char**argv) {
	struct event_base * e = NULL;
	int fd;
	struct sockaddr_in sa;
	int reuse = 1;
	struct event server_ev;
	char * temp = NULL;
	time_t timex;

	/*
	 * Set port
	 */
	if (argc > 1) {
		port = atoi(argv[1]);
		if (!port) {
			fprintf(stderr, "Error: Second port parameter [%s] is invalid\n", argv[1]);
			return (EXIT_FAILURE);
		}
	}
	else if ((temp = getenv("PWDHTTPD_PORT"))) {
		port = atoi(temp);
		if (!port) {
			fprintf(stderr, "Error: Environment port parameter PWDHTTPD_PORT[%s] is invalid\n", temp);
			return (EXIT_FAILURE);
		}
	}
	else {
		port = 80;
	}

	time(&timex);
	temp = ctime(&timex);
	temp[strlen(temp)-1] = '\0';
	fprintf(stderr, "pwdhttpd starting on %s with PID %u\n", temp, getpid());

	/*
	 * Setup
	 */
	setvbuf(stdout, NULL, _IONBF, 0);
	signal(SIGPIPE, &sig_pipe);

	/*
	 * Prepare a listening server socket
	 */
	if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) == -1) {
		perror("Error creating TCP socket");
		return (EXIT_FAILURE);
	}
	setnonblock(fd);
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

	fprintf(stderr, "Binding to port [%u] ... ", port);
	bzero(&sa, sizeof(sa));
	sa.sin_family = AF_INET;
	sa.sin_addr.s_addr = htonl(INADDR_ANY);
	sa.sin_port = htons(port);
	if (bind(fd, (struct sockaddr*) &sa, sizeof(sa)) == -1) {
		perror("Error binding to port");
		return (EXIT_FAILURE);
	}
	else {
		fprintf(stderr, "done\n");
	}

	if (listen(fd,LISTENQ) == -1) {
		perror("Error listening to port");
		return (EXIT_FAILURE);
	}

	/*
	 *  Create event loop and add server socket to it
	 */
	e = event_init();
	event_priority_init(5);

	event_set(&server_ev, fd, EV_READ|EV_PERSIST, &cb_newconnection, &server_ev);
	event_priority_set(&server_ev, 1);
	event_add(&server_ev, NULL);

	/*
	 * Advertise via sexy bonjour
	 */
#ifdef HAVE_DNS_SD_H
	DNSServiceRef d_sr = NULL;
	int res = 0;
	if ((res = DNSServiceRegister(&d_sr, 0, 0, NULL, "_http._tcp", NULL, NULL, htons(port), 0, NULL, NULL, NULL)) != kDNSServiceErr_NoError) {
		fprintf(stderr, "Warning: Failed to register bonjour service.  Error code [%d]\n", res);
	}
	else {
		fprintf(stderr, "Successfully registered Bonjour service.\n");
	}
#endif

	/*
	 * And off we go
	 */
	fprintf(stderr, "Ready to accept connections on port %u\n\n", port);
	event_dispatch();

	/*
	 * Cleanup
	 */
#ifdef HAVE_DNS_SD_H
	if (d_sr) {
		DNSServiceRefDeallocate(d_sr);
	}
#endif

	return(0);
}
