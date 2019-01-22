struct fs_event {
	const char * dir_name;
	/* NOTE: There is no need to save file name, it is 'current' always */
	int watch_dir;
	int watch_file;
	int fd;
	void * data;
	const struct stat_func * func;
};

int is_directory(const char *);

int prepare_fs_event_fd();
void process_fs_event_queue(const int, struct fs_event *, size_t);
