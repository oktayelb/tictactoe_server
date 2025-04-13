#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <poll.h>
#include <sys/wait.h>
#include "print_output.h"

#define PIPE(fd) socketpair(AF_UNIX, SOCK_STREAM, PF_UNIX, fd)

int is_valid_move(int x, int y, char **grid, int width, int height) {
    if (x < 0 || x >= width || y < 0 || y >= height) return 0;
    return grid[y][x] == '.';
}



int check_winner(char **grid, int width, int height, int streak_size, char player_char, int last_x, int last_y) {

    int directions[4][2] = {
        {1, 0},
        {0, 1},
        {1, 1},
        {1, -1}
    };

    for (int d = 0; d < 4; d++) {
        int dx = directions[d][0];
        int dy = directions[d][1];
        int count = 1;

        for (int i = 1; i < streak_size; i++) {
            int nx = last_x + i * dx;
            int ny = last_y + i * dy;
            if (nx >= 0 && nx < width && ny >= 0 && ny < height && grid[ny][nx] == player_char)
                count++;
            else break;
        }

        for (int i = 1; i < streak_size; i++) {
            int nx = last_x - i * dx;
            int ny = last_y - i * dy;
            if (nx >= 0 && nx < width && ny >= 0 && ny < height && grid[ny][nx] == player_char)
                count++;
            else break;
        }

        if (count >= streak_size)
            return 1;
    }

    return 0;
}

int grid_is_full(char **grid, int width, int height) {
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            if (grid[y][x] == '.') return 0;
        }
    }
    return 1;
}

void game_ended(sm *end_msg, int player_count, int *pipes[], pid_t *pids) {
    for (int i = 0; i < player_count; i++) {
        write(pipes[i][0], end_msg, sizeof(sm));
        smp log = {pids[i], end_msg};
        print_output(NULL, &log, NULL, 0);
    }
}

typedef struct {
    char avatar;
    int arg_count;
    char exec_path[100];
    char *args[8];
    pid_t pid;
    int* fd;
} Player;

int main() {
    int grid_width, grid_height, streak_size, player_size = 0;
    scanf("%d %d %d %d\n", &grid_width, &grid_height, &streak_size, &player_size);

    char **grid = (char**)malloc(grid_height * sizeof(char*));
    for (int i = 0; i < grid_height; i++) {
        grid[i] = (char*)malloc(grid_width * sizeof(char));
        for (int j = 0; j < grid_width; j++) {
            grid[i][j] = '.';
        }
    }

    Player players[player_size];
    char line[140]; // 
    int pipes[player_size][2];
    int* parent_pipes[player_size];
    
    int player_id = 0;
    gu update_array[grid_height * grid_width];
    int filled_grid_count = 0;

    pid_t *pids = malloc(sizeof(pid_t) * player_size);

    while (player_id < player_size && fgets(line, sizeof(line), stdin)) {
        char avatar;
        int arg_count;
        sscanf(line, " %c %d", &avatar, &arg_count);

        players[player_id].avatar = avatar;
        players[player_id].arg_count = arg_count;

        if (!fgets(line, sizeof(line), stdin)) {
            break;
        }

        char *token = strtok(line, " \n");
        if (token) {
            strcpy(players[player_id].exec_path, token);
            players[player_id].args[0] = strdup(players[player_id].exec_path);
            
            int i = 1;
            while ((token = strtok(NULL, " \n")) != NULL && i < 8) {
                players[player_id].args[i++] = strdup(token);
            }
            players[player_id].args[i] = NULL;
        }
        player_id++;
    }

    for (int i = 0; i < player_size; i++) {
        if (PIPE(pipes[i]) < 0) {
            printf("Error Creating Sockets\n");
            return 1;
        }
        players[i].fd = pipes[i];
        parent_pipes[i] = &pipes[i][0];
    }

    for (int i = 0; i < player_size; i++) {
        pid_t pid = fork();

        if (pid == 0) {
            pid_t child_pid = getpid();

            for (int j = 0; j < player_size; j++) {
                if (j != i) {
                    close(pipes[j][0]);
                    close(pipes[j][1]);
                } else {
                    close(pipes[j][0]);
                }
            }

            dup2(pipes[i][1], STDIN_FILENO);
            dup2(pipes[i][1], STDOUT_FILENO);
            write(pipes[i][1], &child_pid, sizeof(pid_t));
            close(pipes[i][1]);

            execv(players[i].exec_path, players[i].args);
        } else {
            pid_t child_pid;

            close(pipes[i][1]);
            read(pipes[i][0],&child_pid, sizeof(pid_t));
            pids[i] = child_pid;
            players[i].pid = pid;
        }
    }

    struct pollfd fds[player_size];
    for (int i = 0; i < player_size; i++) {
        fds[i].fd = pipes[i][0];
        fds[i].events = POLLIN;
    }

    int game_over = 0;
    
    while (!game_over) {
        int ready = poll(fds, player_size, 1);

        if (ready > 0) {
            for (int i = 0; i < player_size; i++) {
                if (fds[i].revents & POLLIN) {
                    cm client_msg;
                    ssize_t n = read(fds[i].fd, &client_msg, sizeof(client_msg));
                    
                    if (n <= 0) continue;
                    
                    cmp msg;
                    msg.process_id = pids[i];
                    msg.client_message = &client_msg;
                    print_output(&msg, NULL, NULL, 0);

                    if (client_msg.type == START) {
                        sm send_message;
                        send_message.type = RESULT;
                        send_message.success = 0;
                        send_message.filled_count = filled_grid_count;

                        smp msg_prnt;
                        msg_prnt.process_id = pids[i];
                        msg_prnt.server_message = &send_message;
                        
                        write(pipes[i][0], &send_message, sizeof(sm));
                        
                        if (filled_grid_count > 0) {
                            write(pipes[i][0], update_array, filled_grid_count * sizeof(gu));
                        }
                        
                        print_output(NULL, &msg_prnt, update_array, filled_grid_count);
                    } 
                    else if (client_msg.type == MARK) {
                        coordinate pos = client_msg.position;
                        
                        int success = is_valid_move(pos.x, pos.y, grid, grid_width, grid_height);
                        
                        if (success) {
                            grid[pos.y][pos.x] = players[i].avatar;
                            
                            gu new_update;
                            new_update.position = pos;
                            new_update.character = players[i].avatar;
                            update_array[filled_grid_count] = new_update;
                            filled_grid_count++;
                        }
                        
                        sm send_message;
                        send_message.type = RESULT;
                        send_message.success = success;
                        send_message.filled_count = filled_grid_count;
                        
                        smp msg_prnt;
                        msg_prnt.process_id = pids[i];
                        msg_prnt.server_message = &send_message;
                        
                        if (success && check_winner(grid, grid_width, grid_height, streak_size, players[i].avatar, pos.x, pos.y)) {
                            send_message.type = END;
                            game_over = 1;
                            
                            game_ended(&send_message, player_size, parent_pipes, pids);
                            printf("Winner: Player %c\n", players[i].avatar);
                        }
                        else if (success && grid_is_full(grid, grid_width, grid_height)) {
                            send_message.type = END;
                            game_over = 1;
                            
                            game_ended(&send_message, player_size, parent_pipes, pids);
                            printf("Draw\n");
                        }
                        else {
                            write(pipes[i][0], &send_message, sizeof(sm));
                            
                            if (filled_grid_count > 0) {
                                write(pipes[i][0], update_array, filled_grid_count * sizeof(gu));
                            }
                            
                            print_output(NULL, &msg_prnt, update_array, filled_grid_count);
                        }
                    }
                }
            }
        }
    }
    
        // Free allocated memory
    for (int i = 0; i < grid_height; i++) {
        free(grid[i]);
    }
    free(grid);
    free(pids);
    
    for (int i = 0; i < player_size; i++) {
        for (int j = 0; players[i].args[j] != NULL; j++) {
            free(players[i].args[j]);
        }
    }

    return 0;
}
