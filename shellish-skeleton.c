#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <termios.h> // termios, TCSANOW, ECHO, ICANON
#include <unistd.h>
#include <fcntl.h> // for open()
#include <ctype.h> // isdigit
#include <dirent.h> // opedndir, readdir
#include <sys/stat.h> // mkdir, mkfifo, stat
#include <sys/types.h> 
#include <signal.h> // kill, SIGTERM
const char *sysname = "shellish";

enum return_codes {
  SUCCESS = 0,
  EXIT = 1,
  UNKNOWN = 2,
};

struct command_t {
  char *name;
  bool background;
  bool auto_complete;
  int arg_count;
  char **args;
  char *redirects[3];     // in/out redirection
  struct command_t *next; // for piping
};

/**
 * Prints a command struct
 * @param struct command_t *
 */
void print_command(struct command_t *command) {
  int i = 0;
  printf("Command: <%s>\n", command->name);
  printf("\tIs Background: %s\n", command->background ? "yes" : "no");
  printf("\tNeeds Auto-complete: %s\n", command->auto_complete ? "yes" : "no");
  printf("\tRedirects:\n");
  for (i = 0; i < 3; i++)
    printf("\t\t%d: %s\n", i,
           command->redirects[i] ? command->redirects[i] : "N/A");
  printf("\tArguments (%d):\n", command->arg_count);
  for (i = 0; i < command->arg_count; ++i)
    printf("\t\tArg %d: %s\n", i, command->args[i]);
  if (command->next) {
    printf("\tPiped to:\n");
    print_command(command->next);
  }
}

/**
 * Release allocated memory of a command
 * @param  command [description]
 * @return         [description]
 */
int free_command(struct command_t *command) {
  if (command->arg_count) {
    for (int i = 0; i < command->arg_count; ++i)
      free(command->args[i]);
    free(command->args);
  }
  for (int i = 0; i < 3; ++i)
    if (command->redirects[i])
      free(command->redirects[i]);
  if (command->next) {
    free_command(command->next);
    command->next = NULL;
  }
  free(command->name);
  free(command);
  return 0;
}

/**
 * Show the command prompt
 * @return [description]
 */
int show_prompt() {
  char cwd[1024], hostname[1024];
  gethostname(hostname, sizeof(hostname));
  getcwd(cwd, sizeof(cwd));
  printf("%s@%s:%s %s$ ", getenv("USER"), hostname, cwd, sysname);
  return 0;
}

/**
 * Parse a command string into a command struct
 * @param  buf     [description]
 * @param  command [description]
 * @return         0
 */
int parse_command(char *buf, struct command_t *command) {
  const char *splitters = " \t"; // split at whitespace
  int index, len;
  len = strlen(buf);
  while (len > 0 && strchr(splitters, buf[0]) != NULL) // trim left whitespace
  {
    buf++;
    len--;
  }
  while (len > 0 && strchr(splitters, buf[len - 1]) != NULL)
    buf[--len] = 0; // trim right whitespace

  if (len > 0 && buf[len - 1] == '?') // auto-complete
    command->auto_complete = true;
  if (len > 0 && buf[len - 1] == '&') // background
    command->background = true;

  char *saveptr = NULL;
  char *pch = strtok_r(buf, splitters, &saveptr);
  if (pch == NULL) {
    command->name = (char *)malloc(1);
    command->name[0] = 0;
  } else {
    command->name = (char *)malloc(strlen(pch) + 1);
    strcpy(command->name, pch);
  }

  command->args = (char **)malloc(sizeof(char *));

  int redirect_index;
  int arg_index = 0;
  char temp_buf[1024], *arg;
  while (1) {
    // tokenize input on splitters
    pch = strtok_r(NULL, splitters, &saveptr);
    if (!pch)
      break;
    arg = temp_buf;
    strcpy(arg, pch);
    len = strlen(arg);

    if (len == 0)
      continue; // empty arg, go for next
    while (len > 0 && strchr(splitters, arg[0]) != NULL) // trim left whitespace
    {
      arg++;
      len--;
    }
    while (len > 0 && strchr(splitters, arg[len - 1]) != NULL)
      arg[--len] = 0; // trim right whitespace
    if (len == 0)
      continue; // empty arg, go for next

// piping to another command
if (strcmp(arg, "|") == 0) {
  struct command_t *c = (struct command_t *)malloc(sizeof(struct command_t));
  memset(c, 0, sizeof(struct command_t));

  // text after "|" is parsed as the next command 
  while (*saveptr == ' ' || *saveptr == '\t') saveptr++; // skip whitespace
  parse_command(saveptr, c);

  command->next = c;
  break; // stop parsing args for the left command
}

    // background process
    if (strcmp(arg, "&") == 0)
      continue; // handled before

    // handle input redirection
    redirect_index = -1;
    if (arg[0] == '<')
      redirect_index = 0;
    if (arg[0] == '>') {
      if (len > 1 && arg[1] == '>') {
        redirect_index = 2;
        arg++;
        len--;
      } else
        redirect_index = 1;
    }
    if (redirect_index != -1) {
      command->redirects[redirect_index] = (char *)malloc(len);
      strcpy(command->redirects[redirect_index], arg + 1);
      continue;
    }

    // normal arguments
    if (len > 2 &&
        ((arg[0] == '"' && arg[len - 1] == '"') ||
         (arg[0] == '\'' && arg[len - 1] == '\''))) // quote wrapped arg
    {
      arg[--len] = 0;
      arg++;
    }
    command->args =
        (char **)realloc(command->args, sizeof(char *) * (arg_index + 1));
    command->args[arg_index] = (char *)malloc(len + 1);
    strcpy(command->args[arg_index++], arg);
  }
  command->arg_count = arg_index;

  // increase args size by 2
  command->args = (char **)realloc(command->args,
                                   sizeof(char *) * (command->arg_count += 2));

  // shift everything forward by 1
  for (int i = command->arg_count - 2; i > 0; --i)
    command->args[i] = command->args[i - 1];

  // set args[0] as a copy of name
  command->args[0] = strdup(command->name);
  // set args[arg_count-1] (last) to NULL
  command->args[command->arg_count - 1] = NULL;

  return 0;
}

void prompt_backspace() {
  putchar(8);   // go back 1
  putchar(' '); // write empty over
  putchar(8);   // go back 1 again
}

/**
 * Prompt a command from the user
 * @param  buf      [description]
 * @param  buf_size [description]
 * @return          [description]
 */
int prompt(struct command_t *command) {
  int index = 0;
  char c;
  char buf[4096];
  static char oldbuf[4096];

  // tcgetattr gets the parameters of the current terminal
  // STDIN_FILENO will tell tcgetattr that it should write the settings
  // of stdin to oldt
  static struct termios backup_termios, new_termios;
  tcgetattr(STDIN_FILENO, &backup_termios);
  new_termios = backup_termios;
  // ICANON normally takes care that one line at a time will be processed
  // that means it will return if it sees a "\n" or an EOF or an EOL
  new_termios.c_lflag &=
      ~(ICANON |
        ECHO); // Also disable automatic echo. We manually echo each char.
  // Those new settings will be set to STDIN
  // TCSANOW tells tcsetattr to change attributes immediately.
  tcsetattr(STDIN_FILENO, TCSANOW, &new_termios);

  show_prompt();
  buf[0] = 0;
  while (1) {
    c = getchar();
    // printf("Keycode: %u\n", c); // DEBUG: uncomment for debugging

    if (c == 9) // handle tab
    {
      buf[index++] = '?'; // autocomplete
      break;
    }

    if (c == 127) // handle backspace
    {
      if (index > 0) {
        prompt_backspace();
        index--;
      }
      continue;
    }

    if (c == 27 || c == 91 || c == 66 || c == 67 || c == 68) {
      continue;
    }

    if (c == 65) // up arrow
    {
      while (index > 0) {
        prompt_backspace();
        index--;
      }

      char tmpbuf[4096];
      printf("%s", oldbuf);
      strcpy(tmpbuf, buf);
      strcpy(buf, oldbuf);
      strcpy(oldbuf, tmpbuf);
      index += strlen(buf);
      continue;
    }

    putchar(c); // echo the character
    buf[index++] = c;
    if (index >= sizeof(buf) - 1)
      break;
    if (c == '\n') // enter key
      break;
    if (c == 4) // Ctrl+D
      return EXIT;
  }
  if (index > 0 && buf[index - 1] == '\n') // trim newline from the end
    index--;
  buf[index++] = '\0'; // null terminate string

  strcpy(oldbuf, buf);

  parse_command(buf, command);

  // print_command(command); // DEBUG: uncomment for debugging

  // restore the old settings
  tcsetattr(STDIN_FILENO, TCSANOW, &backup_termios);
  return SUCCESS;
}

static char *resolve_in_path(const char *cmd) {
  // return null if command is empty
  if (!cmd || cmd[0] == '\0') return NULL;

  // if command contains '/', treat as direct path
  if (strchr(cmd, '/')) {
    if (access(cmd, X_OK) == 0) return strdup(cmd);
    return NULL;
  }

  const char *path_env = getenv("PATH"); // get PATH enviorment variable
  if (!path_env) return NULL;

  char *paths = strdup(path_env); // duplicate PATH because strtok modifies
  if (!paths) return NULL;

  char *saveptr = NULL;
  for (char *dir = strtok_r(paths, ":", &saveptr); // itarate over the directories
       dir != NULL;
       dir = strtok_r(NULL, ":", &saveptr)) {

    // full path construction    
    size_t need = strlen(dir) + 1 + strlen(cmd) + 1;
    char *full = (char *)malloc(need);
    if (!full) continue;

    snprintf(full, need, "%s/%s", dir, cmd);

    if (access(full, X_OK) == 0) { // check executable
      free(paths);
      return full; // caller will free
    }
    free(full);
  }

  free(paths);
  return NULL;
}

// part 3b-b: chatroom builtin
static void ensure_dir_exists(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) return;
  mkdir(path, 0777); // if fails, next operations will fail 
}

static void ensure_fifo_exists(const char *path) {
  struct stat st;
  if (stat(path, &st) == 0) return; // exists
  mkfifo(path, 0666);
}

// child: write a message to one fifo
static void write_one_fifo(const char *fifo_path, const char *msg) {
  int fd = open(fifo_path, O_WRONLY | O_NONBLOCK);
  if (fd < 0) _exit(0); // ignore if nobody reads
  write(fd, msg, strlen(msg));
  close(fd);
  _exit(0);
}
static int run_chatroom(struct command_t *command) {
  // chatroom <roomname> <username>
  if (command->arg_count < 3) return SUCCESS;

  const char *room = command->args[1];
  const char *user = command->args[2];

  char room_dir[512];
  snprintf(room_dir, sizeof(room_dir), "/tmp/chatroom-%s", room);
  ensure_dir_exists(room_dir); // create room folder if needed with contentReference[oaicite:3]{index=3}

  char my_fifo[512];
  snprintf(my_fifo, sizeof(my_fifo), "%s/%s", room_dir, user);
  ensure_fifo_exists(my_fifo); // create user fifo if needed with contentReference[oaicite:4]{index=4}

  printf("Welcome to %s!\n", room);

  // reader child continuously reads from own fifo with contentReference[oaicite:5]{index=5}
  pid_t reader = fork();
  if (reader == 0) {
    // open RDWR for no writes yet
    int fd = open(my_fifo, O_RDWR);
    if (fd < 0) _exit(1);

    FILE *fp = fdopen(fd, "r");
    if (!fp) _exit(1);

    char line[4096];
    while (fgets(line, sizeof(line), fp)) {
      fputs(line, stdout);
      fflush(stdout);
    }
    _exit(0);
  }

  // writer loop: type messages; send to others via separate children with contentReference[oaicite:6]{index=6}
  char input[2048];
  while (1) {
    printf("[%s] %s > ", room, user);
    fflush(stdout);

    if (!fgets(input, sizeof(input), stdin)) break;

    size_t L = strlen(input);
    if (L > 0 && input[L - 1] == '\n') input[L - 1] = '\0';

    if (strcmp(input, "exit") == 0) break;

    char msg[4096];
    snprintf(msg, sizeof(msg), "[%s] %s: %s\n", room, user, input);

    DIR *d = opendir(room_dir);
    if (!d) continue;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
      if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0) continue;
      if (strcmp(ent->d_name, user) == 0) continue; // don't write to self

      char target[512];
      snprintf(target, sizeof(target), "%s/%s", room_dir, ent->d_name);

      pid_t s = fork(); // separate child per recipient w,th contentReference[oaicite:7]{index=7}
      if (s == 0) write_one_fifo(target, msg);
    }
    closedir(d);

    // reap finished sender children for avoiding zombies
    while (waitpid(-1, NULL, WNOHANG) > 0) {}
  }
  // stopped reader child so it doesn't keep printing other rooms when executed
  kill(reader, SIGTERM);
  waitpid(reader, NULL, 0);

  // remove the fifo so others in the room won't keep writing 
  unlink(my_fifo);

  return SUCCESS;
}



int process_command(struct command_t *command) {
  int r;
  if (strcmp(command->name, "") == 0) 
    return SUCCESS;

  if (strcmp(command->name, "exit") == 0) 
    return EXIT;

if (strcmp(command->name, "cd") == 0) { 
    if (command->arg_count > 0) {
      r = chdir(command->args[1]);
      if (r == -1)
        printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
      return SUCCESS;
    }
  }
    if (strcmp(command->name, "chatroom") == 0) { // part 3b-b extension
    return run_chatroom(command);
  }

  // part 3b-a: cut builtin, read input, split by delimeer and print requested
  if (strcmp(command->name, "cut") == 0) {

    char delim = '\t';
    char *fields = NULL;

    // parse args
    for (int i = 1; i < command->arg_count - 1; i++) {
      if (strcmp(command->args[i], "-d") == 0 && command->args[i+1])
        delim = command->args[i+1][0];

      if (strcmp(command->args[i], "-f") == 0 && command->args[i+1])
        fields = command->args[i+1];
    }

    if (!fields) return SUCCESS;

    int f1 = atoi(strtok(fields, ","));
    int f2 = atoi(strtok(NULL, ","));

    char line[4096];
    while (fgets(line, sizeof(line), stdin)) {

      char *token;
      int index = 1;

      token = strtok(line, &delim);

      while (token) {

        if (index == f1 || index == f2)
          printf("%s", token);

        token = strtok(NULL, &delim);
        index++;

        if ((index == f2+1))
          printf("%c", delim);
      }

      printf("\n");
    }

    return SUCCESS;
  }

  // Part 3a: piping
if (command->next != NULL) {
  int fd[2];
  if (pipe(fd) < 0) {
    printf("-%s: pipe: %s\n", sysname, strerror(errno));
    return SUCCESS;
  }

  pid_t left = fork();
  if (left == 0) {
    // left command: stdout -> pipe write
    dup2(fd[1], STDOUT_FILENO);
    close(fd[0]);
    close(fd[1]);

    // apply redirections on left 
    char *fullpath = resolve_in_path(command->name);
    if (!fullpath) {
      printf("-%s: %s: command not found\n", sysname, command->name);
      exit(127);
    }
    execv(fullpath, command->args);
    printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
    free(fullpath);
    exit(127);
  }

  pid_t right = fork();
  if (right == 0) {
    // right side: stdin <- pipe read
    dup2(fd[0], STDIN_FILENO);
    close(fd[0]);
    close(fd[1]);

    // recursively execute the rest pipeline
    exit(process_command(command->next));
  }

  // parent
  close(fd[0]);
  close(fd[1]);

  if (command->background) {
    waitpid(left, NULL, WNOHANG);
    waitpid(right, NULL, WNOHANG);
    return SUCCESS;
  }

  waitpid(left, NULL, 0);
  waitpid(right, NULL, 0);
  return SUCCESS;
}
  pid_t pid = fork(); 
  if (pid == 0) // child
  {
    /// This shows how to do exec with environ (but is not available on MacOs)
    // extern char** environ; // environment variables
    // execvpe(command->name, command->args, environ); // exec+args+path+environ

    /// This shows how to do exec with auto-path resolve
    // add a NULL argument to the end of args, and the name to the beginning
    // as required by exec

    // TODO: do your own exec with path resolving using execv()
    // do so by replacing the execvp call below
    char *fullpath = resolve_in_path(command->name); // resolve command full path manually
    if (!fullpath) {
      printf("-%s: %s: command not found\n", sysname, command->name);
      exit(127);
    }
    // PART 2: redirections
    if (command->redirects[0]) { // <input
        int fd = open(command->redirects[0], O_RDONLY);
        if (fd < 0) {
            printf("-%s: %s: %s\n", sysname, command->redirects[0], strerror(errno));
            exit(1);
        }
        dup2(fd, STDIN_FILENO);
        close(fd);
    }

    if (command->redirects[1]) { // >output overwrite
        int fd = open(command->redirects[1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            printf("-%s: %s: %s\n", sysname, command->redirects[1], strerror(errno));
            exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    if (command->redirects[2]) { //>>output append
        int fd = open(command->redirects[2], O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd < 0) {
            printf("-%s: %s: %s\n", sysname, command->redirects[2], strerror(errno));
            exit(1);
        }
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }

    execv(fullpath, command->args); //execute with arguements

    // execution failed error
    printf("-%s: %s: %s\n", sysname, command->name, strerror(errno));
    free(fullpath);
    exit(127);
  } else {
    // TODO: implement background processes here
    if (command->background) { // if command has '&', run in background
      waitpid(pid, NULL, WNOHANG);
      return SUCCESS;
}
      // execute in foreground wait until child finish
      waitpid(pid, NULL, 0);
      return SUCCESS;
      }
}

int main() {
  while (1) {
    struct command_t *command =
        (struct command_t *)malloc(sizeof(struct command_t));
    memset(command, 0, sizeof(struct command_t)); // set all bytes to 0

    int code;
    code = prompt(command);
    if (code == EXIT)
      break;

    code = process_command(command);
    if (code == EXIT)
      break;

    free_command(command);
  }

  printf("\n");
  return 0;
}
