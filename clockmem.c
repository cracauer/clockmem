#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <sys/mman.h>

#ifdef MINCORE_MINCORE
const int INCORE = MINCORE_MINCORE;
#else
const int INCORE = 1;
#endif

enum ops {OP_MLOCK = 0, OP_INCORE, OP_JUSTMAP, OP_SWEEP};

struct options {
  int verbose;
  int touch_all_pages;
  int op;
  size_t anonymous;
};
  

struct onefile {
  char *name;
  int fd;
  size_t length;
  size_t paged_size;
  size_t mapped_size;
  long long n_pages;
  void *data;
};

void do_anonymous(const struct options *o)
{
  void *map;

  printf("Doing mmap of %f GB:",
	 (float)o->anonymous / 1024.0 / 1024.0 / 1024.0);
  fflush(stdout);
  map = mmap(NULL, o->anonymous, PROT_READ|PROT_WRITE, MAP_ANON|MAP_PRIVATE, -1, 0);
  if (map == MAP_FAILED) {
    perror("mmap anon");
    exit(2);
  }
  printf(".\n");
  fflush(stdout);
  printf("Doing mlock:");
  fflush(stdout);
  if (mlock(map, o->anonymous) == -1) {
    perror("mlock anon");
    exit(2);
  }
  printf(".\n");
  fflush(stdout);
  printf("mapped and mlocked anonymously @ %p-%p, waiting for getchar\n? ",
	 map, map+o->anonymous);
  fflush(stdout);
  getchar();

  printf("Doing munlock:");
  fflush(stdout);
  if (munlock(map, o->anonymous) == -1) {
    perror("munlock anon");
    exit(2);
  }
  printf(".\n");
  fflush(stdout);
  printf("Doing munmap:");
  fflush(stdout);
  if (munmap(map, o->anonymous)) {
    perror("munmap");
    exit(2);
  }
  printf(".\n");
  fflush(stdout);
  exit(0);
}

int usage(FILE *f)
{
  fprintf(f,
	  "Usage: -v <level> -o [l|i] -t [files]\n"
	  "Usage: -a <bbytes> # just mmap and lock n bytes anonymously\n"

	  "\n"
	  "At this time there are two major modes:\n"
	  "-o l (default) = lock all pages in all files\n"
	  "(last fractual page might be omitted)\n"
	  "\n"

	  "-o i = do not lock, but print a count how many pages in the\n"
	  "       file are already in memory.  Can be used with -t.\n"
	  "       Use -v 1 to print per file, -v 0 only gives total\n"
	  "\n"

	  "-o m = just mmap the file read-only and wait\n"
	  "\n"

	  "-t = touch all pages in all files after mapping (bring into mem)\n"
	  "\n"

	  "-v 0 = silent (unless requested)\n"
	  "-v 1 = not per file\n"
	  "-v 2 = per file noise\n"
	  "-v 3 = more noise\n"
	  "-v 4 = most noise\n"
	  );



  exit(1);
}

int main(int argc, char *argv[])
{
  int i;
  struct onefile *files; /* Zero-terminated on ->name */
  int n_files;
  int pagesize;
  struct options o;
  int ch;
  long long total_pages = 0;
  long long total_pages_mapped = 0;
  long long useless_counter = 0;

  pagesize = getpagesize();

  o.verbose = 0;
  o.touch_all_pages = 0;
  o.op = OP_MLOCK;
  o.anonymous = 0;

  while ((ch = getopt(argc, argv, "a:ho:tv:")) != -1) {
    switch (ch) {
    case 'h':
      usage(stdout);
    case 'a':
      o.anonymous = atol(optarg);
      break;
    case 'o':
      switch (optarg[0]) {
      case 'l': 
	o.op = OP_MLOCK;
	break;
      case 'i':
	o.op = OP_INCORE;
	break;
      case 'm':
	o.op = OP_JUSTMAP;
	break;
      case 's':
	o.op = OP_SWEEP;
	break;
      default:
	fprintf(stderr, "Unknown operation\n");
	usage(stderr);
      }
      break;
    case 't':
      o.touch_all_pages = 1;
      break;
    case 'v':
      o.verbose = atoi(optarg);
      break;
    default:
      usage(stderr);
    }
  }
  argc -= optind;
  argv += optind;

  n_files = argc;

  if (n_files == 0 && o.anonymous != 0)
    do_anonymous(&o);

  files = malloc(sizeof(struct onefile) * n_files + 1);
  if (files == NULL) {
    perror("malloc");
    exit(2);
  }
  files[n_files].name = NULL;

  for (i = 0; i < n_files; i++) {
    struct onefile *file = &files[i];
    struct stat stat;

    file->name = argv[i];
    if (o.verbose > 2)
      printf("Working on file '%s'\n", file->name);

    file->fd = open(file->name, O_RDONLY);
    if (file->fd == -1) {
      fprintf(stderr, "open('%s'): '%s'.  Continuing with "
	      "other files.\n", file->name, strerror(errno));
      continue;
    }

    if (fstat(file->fd, &stat) == -1) {
      fprintf(stderr, "fstat('%s'): '%s'.  Continuing with "
	      "other files.\n", file->name, strerror(errno));
      close(file->fd);
      file->fd = -1;
      continue;
    }
    file->length = stat.st_size;

    if (S_ISDIR(stat.st_mode)) {
      if (o.verbose > 1)
	fprintf(stderr, "'%s' is a directory, dropping it.\n", file->name);
      close(file->fd);
      file->fd = -1;
      continue;
    }

    if (file->length % pagesize == 0)
      file->paged_size = file->length;
    else
      file->paged_size = (file->length / pagesize + 1) * pagesize;
    file->mapped_size = file->paged_size;
    file->n_pages = file->paged_size / pagesize;

    total_pages += file->n_pages;

    if (o.verbose > 3)
      printf("'%s': %lld -> %lld -> %lld\n", file->name
	     , (long long)file->length, (long long)file->paged_size
	     , (long long)file->mapped_size);

    if (file->mapped_size == 0) {
      if (o.verbose > 1)
	fprintf(stderr, "Size of file '%s' is too small, dropping it.\n"
		, file->name);
      close(file->fd);
      file->fd = -1;
      continue;
    }

    file->data = mmap(0, file->mapped_size, PROT_READ, MAP_SHARED, file->fd
		      , 0);
    if (file->data == MAP_FAILED) {
      fprintf(stderr, "mmap('%s' size %lld): '%s'.  Continuing with "
	      "other files.\n", file->name, (long long)file->mapped_size
	      , strerror(errno));
      close(file->fd);
      file->fd = -1;
      continue;
    }

    if (o.touch_all_pages) {
      int sum = 0;
      int *p;
      long long li;
      for (li = 0; li < file->mapped_size / pagesize; li++) {
	p = (int *)(file->data + li * (long long)pagesize);
	sum += *p;
      }
      if (o.verbose > 3) {
	printf("Sum of file '%s': %d\n", file->name, sum);
      }
    }

    if (o.op == OP_MLOCK) {
      if (mlock(file->data, file->paged_size - pagesize) == -1) {
	fprintf(stderr, "mlock('%s'): '%s' (%d) at %p.  Continuing with "
		"other files.\n", file->name, strerror(errno), errno, 
		file->data);
	close(file->fd);
	file->fd = -1;
	continue;
      }
    }

    if (o.op == OP_SWEEP) {
      int *iterate;

      for (iterate = file->data; iterate < (int*)(file->data + file->mapped_size); iterate++) {
	if (*iterate == 42) {
	  useless_counter++;
	}
      }
    }

    if (o.op == OP_INCORE) {
      char *vec;
      long long li;
      int pages_mapped = 0;

      vec = malloc(file->mapped_size / pagesize);
      if (vec == NULL) {
	perror("malloc");
	exit(2);
      }

      if (mincore(file->data, file->mapped_size, vec) == -1) {
	fprintf(stderr, "mincore('%s'): '%s' (%d) at %p.  Continuing with "
		"other files.\n", file->name, strerror(errno), errno, 
		file->data);
	close(file->fd);
	file->fd = -1;
	continue;
      }

      pages_mapped = 0;
      for (li = 0; li < file->mapped_size / pagesize; li++) {
	if (vec[li] & INCORE)
	  pages_mapped++;
      }

      if (o.verbose > 0)
	printf("%.1f %% %lld pages %lld resident: %s\n"
	       , (double)pages_mapped / (double)file->mapped_size 
	         * (double)pagesize * 100.0
	       , (long long)file->mapped_size / (long long)pagesize
	       , (long long)pages_mapped
	       , file->name
	       );

      total_pages_mapped += pages_mapped;

      if (munmap(file->data, file->mapped_size) == -1)
	perror("munmap, continuing");
      if (close(file->fd) == -1)
	perror("munmap, continuing");
      file->fd = -1;
      free(vec);
    }

    if (o.verbose > 2)
      printf("Success for file '%s'\n", file->name);

  }

  free(files);

  if (total_pages_mapped > 0 || o.verbose > 1)
    printf("%.1f %% %lld pages %lld resident: <ALLFILES> \n"
	   , (double)total_pages_mapped / (double)total_pages * 100.0
	   , total_pages
	   , total_pages_mapped
	   );

  if (o.op == OP_MLOCK) {
    printf("Holding %lld pages, waiting for getchar()\n", total_pages);
    getchar();
  }

  if (o.op == OP_JUSTMAP) {
    printf("Mapped %lld pages, waiting for getchar()\n", total_pages);
    getchar();
  }

  if (o.verbose > 0 && o.op == OP_SWEEP) {
    printf("Number of 42s: %lld\n", useless_counter);
  }

  return 0;
}
