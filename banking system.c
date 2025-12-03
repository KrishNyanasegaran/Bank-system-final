#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h> 

#define DB_DIR "database"
#define INDEX_FILE "database/index.txt"
#define LOG_FILE "database/transaction.log"
#define HELP_REQ_FILE "database/help_requests.txt"

typedef struct {
    char name[100];
    char id[16];       
    char type[10];     
    char pin[5];       
    double balance;
    char accNum[12];   
} Account;

/* ---------- Utility I/O helpers ---------- */

static void ensureDatabase() {
#ifdef _WIN32
    system("mkdir database >nul 2>nul");
#else
    system("mkdir -p database");
#endif
    FILE *f = fopen(INDEX_FILE, "a"); if (f) fclose(f);
    f = fopen(LOG_FILE, "a"); if (f) fclose(f);
    f = fopen(HELP_REQ_FILE, "a"); if (f) fclose(f);
}

/* read a line from stdin, trim newline */
static void readLine(char *buf, size_t n) {
    if (fgets(buf, (int)n, stdin) == NULL) {
        buf[0] = '\0';
        return;
    }
    buf[strcspn(buf, "\n")] = 0;
}

/* check if string contains only digits */
static bool isDigits(const char *s) {
    if (!s || *s == '\0') return false;
    for (; *s; ++s) if (!isdigit((unsigned char)*s)) return false;
    return true;
}

/* append entry to transaction log with timestamp */
static void appendLog(const char *entry) {
    FILE *f = fopen(LOG_FILE, "a");
    if (!f) return;
    time_t t = time(NULL);
    char tb[64];
    struct tm *tm = localtime(&t);
    strftime(tb, sizeof(tb), "%Y-%m-%d %H:%M:%S", tm);
    fprintf(f, "[%s] %s\n", tb, entry);
    fclose(f);
}

/* count accounts from index */
static int countAccounts() {
    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) return 0;
    int count = 0;
    char line[64];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\n' || line[0] == '\0') continue;
        ++count;
    }
    fclose(f);
    return count;
}

/* Check if account number exists in index */
static bool accountExists(const char *acc) {
    if (!isDigits(acc)) return false; // must be digits
    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) return false;
    char line[64];
    bool found = false;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if (strcmp(line, acc) == 0) { found = true; break; }
    }
    fclose(f);
    return found;
}

/* write account record to file path database/<acc>.txt */
static bool saveAccountToFile(const Account *a) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.txt", DB_DIR, a->accNum);
    FILE *f = fopen(path, "w");
    if (!f) return false;
    // store lines: name, id, type, pin, balance
    fprintf(f, "%s\n%s\n%s\n%s\n%.2f\n", a->name, a->id, a->type, a->pin, a->balance);
    fclose(f);
    return true;
}

// Helper function to read a line from file and strip newline.
static bool readLineFromFile(FILE *f, char *buf, size_t n) {
    if (fgets(buf, (int)n, f) == NULL) return false;
    buf[strcspn(buf, "\n")] = 0;
    return true;
}


/* load account from file; returns true on success */
static bool loadAccountFromFile(const char *accNum, Account *out) {
    if (!accountExists(accNum)) return false;
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.txt", DB_DIR, accNum);
    FILE *f = fopen(path, "r");
    if (!f) return false;

    // Use a temporary buffer for reading
    char buf[256];
    
    // 1. Name
    if (!readLineFromFile(f, out->name, sizeof(out->name))) goto error_close;
    
    // 2. ID
    if (!readLineFromFile(f, out->id, sizeof(out->id))) goto error_close;
    
    // 3. Type
    if (!readLineFromFile(f, out->type, sizeof(out->type))) goto error_close;
    
    // 4. PIN
    if (!readLineFromFile(f, out->pin, sizeof(out->pin))) goto error_close;
    
    // 5. Balance (Must use fscanf/strtod for numbers)
    double bal = 0.0;
    if (fscanf(f, "%lf", &bal) != 1) goto error_close;
    out->balance = bal;
    
    // Set account number
    strncpy(out->accNum, accNum, sizeof(out->accNum));
    
    fclose(f);
    return true;

error_close:
    fclose(f);
    return false;
}

/* update account file (overwrite) */
static bool updateAccountFile(const Account *a) {
    return saveAccountToFile(a);
}

/* add account number to index file */
static bool appendIndex(const char *acc) {
    FILE *f = fopen(INDEX_FILE, "a");
    if (!f) return false;
    fprintf(f, "%s\n", acc);
    fclose(f);
    return true;
}

/* remove account number from index file */
static bool removeFromIndex(const char *acc) {
    FILE *in = fopen(INDEX_FILE, "r");
    if (!in) return false;
    FILE *tmp = fopen("database/index.tmp", "w");
    if (!tmp) { fclose(in); return false; }

    char line[64];
    bool removed = false;
    while (fgets(line, sizeof(line), in)) {
        line[strcspn(line, "\n")] = 0;
        if (strcmp(line, acc) == 0) { removed = true; continue; }
        if (strlen(line) > 0) fprintf(tmp, "%s\n", line);
    }
    fclose(in);
    fclose(tmp);
    // replace
    remove(INDEX_FILE);
    rename("database/index.tmp", INDEX_FILE);
    return removed;
}

/* ---------- Account number generation (7-9 digits, unique) ---------- */
static void generateAccountNumber(char *out, size_t n) {
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)time(NULL) ^ (unsigned int)getpid());
        seeded = true;
    }
    while (1) {
        int digits = (rand() % 3) + 7; // 7,8,9
        long long val = 0;
        for (int i = 0; i < digits; ++i) {
            int d = rand() % 10;
            // avoid leading zero
            if (i == 0 && d == 0) d = (rand() % 9) + 1;
            val = val * 10 + d;
        }
        snprintf(out, n, "%lld", val);
        if (!accountExists(out)) return;
    }
}

/* small "progress" animation (no real delay; just prints nice bar) */
static void printProgressBar(const char *message) {
    printf("\n%s\n", message);
    printf("[");
    for (int i = 0; i < 20; ++i) {
        if (i < 16) putchar('=');
        else putchar(' ');
    }
    printf("] Done.\n");
}

/* case-insensitive compare for names */
static bool strCaseEqual(const char *a, const char *b) {
    for (; *a && *b; ++a, ++b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    }
    return *a == '\0' && *b == '\0';
}

/* ---------- Validated input helpers ---------- */

/* get 7-digit ID (numbers only) */
static void promptID(char *out, size_t n) {
    while (1) {
        printf("Enter Identification Number (exactly 7 digits): ");
        readLine(out, n);
        if (!isDigits(out)) {
            printf("Error: ID must contain only digits. Try again.\n");
            continue;
        }
        if ((int)strlen(out) != 7) {
            printf("Error: ID must be exactly 7 digits long. You entered %zu digits.\n", strlen(out));
            continue;
        }
        printf("OK: ID accepted.\n");
        break;
    }
}

/* get 4-digit PIN (numbers only) */
static void promptPIN(char *out, size_t n, const char *promptText) {
    (void)n;
    while (1) {
        printf("%s (exactly 4 digits): ", promptText);
        readLine(out, 8); // read more to flush newline if necessary
        if (!isDigits(out)) {
            printf("Error: PIN must contain only digits. Try again.\n");
            continue;
        }
        if (strlen(out) != 4) {
            printf("Error: PIN must be exactly 4 digits.\n");
            continue;
        }
        printf("OK: PIN accepted.\n");
        break;
    }
}

/* prompt for account number (7-9 digits) and ensure it exists */
static void promptExistingAccount(char *out, size_t n) {
    while (1) {
        printf("Enter account number (7-9 digits): ");
        readLine(out, n);
        if (!isDigits(out)) {
            printf("Error: Account numbers must be digits only.\n");
            continue;
        }
        int len = (int)strlen(out);
        if (len < 7 || len > 9) {
            printf("Error: Account number must be between 7 and 9 digits (you entered %d digits).\n", len);
            continue;
        }
        if (!accountExists(out)) {
            printf("Error: Account number %s is not registered.\n", out);
            continue;
        }
        printf("OK: Account %s found.\n", out);
        break;
    }
}

/* prompt for a positive decimal amount (greater than 0) and optional upper limit */
static double promptAmount(const char *promptText, double maxAllowed, bool enforceMax) {
    char buf[64];
    double val;
    while (1) {
        printf("%s: RM ", promptText);
        readLine(buf, sizeof(buf));
        // check string: allow digits and at most one dot
        int dots = 0;
        int i;
        if (buf[0] == '-' ) { printf("Error: negative amounts not allowed.\n"); continue; }
        for (i = 0; buf[i]; ++i) {
            if (buf[i] == '.') { if (++dots > 1) break; continue; }
            if (!isdigit((unsigned char)buf[i])) break;
        }
        if (buf[0] == '\0' || buf[i] != '\0') {
            printf("Error: please enter a valid number (e.g., 10.50). You typed: %s\n", buf);
            continue;
        }
        errno = 0;
        val = strtod(buf, NULL);
        if (errno != 0) { printf("Error: invalid number.\n"); continue; }
        if (!(val > 0.0)) { printf("Error: amount must be greater than RM0.00.\n"); continue; }
        if (enforceMax && val > maxAllowed) {
            printf("Error: amount exceeds the allowed maximum of RM%.2f per operation.\n", maxAllowed);
            continue;
        }
        // valid
        break;
    }
    return val;
}

/* ---------- Core operations ---------- */

/* Helper function to check if a name contains only letters and spaces, minimum length, and at least one space */
int isValidName(const char *name) {
    int space_count = 0;
    int len = (int)strlen(name);

    if (len < 3) return 0; 

    for (int i = 0; name[i] != '\0'; i++) {
        // Check 2: Invalid characters
        if (!isalpha(name[i]) && !isspace(name[i])) {
            return 0; 
        }
        if (isspace(name[i])) {
            // Check 3: Count spaces and ensure no consecutive spaces
            if (i > 0 && isspace(name[i-1])) return 0; 
            space_count++;
        }
    }
    
    // Check 4: Ensure at least one space (e.g., first name and last name)
    if (space_count < 1) return 0;
    
    // Check 5: Ensure name doesn't start or end with a space
    if (isspace(name[0]) || isspace(name[len - 1])) return 0;

    return 1; 
}

static void cmdCreate() {
    Account a;
    memset(&a, 0, sizeof(a));
    printf("\n--- Create New Bank Account ---\n");

    // NEW VALIDATION LOOP
    while (1) { 
        printf("Enter full name (must contain at least a first and last name): ");
        readLine(a.name, sizeof(a.name));

        // 1. Check for empty name
        if (strlen(a.name) == 0) {
            printf("Error: Name cannot be empty. Creation cancelled.\n");
            return; 

        // 2. Check for invalid characters, length, and word count
        } else if (!isValidName(a.name)) {
            printf("Warning: Invalid name format. Name must be letters and spaces only, minimum 3 characters, and contain at least two words (e.g., 'John Smith'). Please re-enter.\n");
           

        // 3. Name is valid
        } else {
            break; 
        }
    }
    
    printf("Name '%s' successfully validated. Continuing account setup...\n", a.name);

    promptID(a.id, sizeof(a.id));
    while (1) {
        printf("Account Type (savings/current): ");
        readLine(a.type, sizeof(a.type));
        for (size_t i=0;i<strlen(a.type);++i) a.type[i]=tolower((unsigned char)a.type[i]);
        if (strcmp(a.type,"savings")==0 || strcmp(a.type,"current")==0) break;
        printf("Error: invalid account type. Enter 'savings' or 'current'.\n");
    }
    promptPIN(a.pin, sizeof(a.pin), "Enter 4-digit PIN");
    a.balance = 0.00;
    generateAccountNumber(a.accNum, sizeof(a.accNum));

    if (!saveAccountToFile(&a)) {
        printf("Error: failed to save account. Check file permissions.\n");
        return;
    }
    if (!appendIndex(a.accNum)) {
        printf("Warning: failed to write index file, account file is created but may not be listed in index.\n");
    }

    char logbuf[256];
    snprintf(logbuf, sizeof(logbuf), "CREATE account %s (Name: %s, Type: %s)", a.accNum, a.name, a.type);
    appendLog(logbuf);

    printf("\nSuccess: Account created!\n");
    printf("Account Number: %s\nInitial Balance: RM%.2f\n", a.accNum, a.balance);
    printProgressBar("Finalizing creation...");
}

static void cmdDelete() {
    printf("\n--- Delete Bank Account ---\n");
    // show list of accounts (reading index)
    FILE *f = fopen(INDEX_FILE, "r");
    if (!f) { printf("No accounts found.\n"); return; }
    printf("Registered accounts:\n");
    char line[64];
    int i = 0;
    while (fgets(line, sizeof(line), f)) {
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) > 0) { printf(" - %s\n", line); ++i; }
    }
    fclose(f);
    if (i==0) { printf("No accounts registered.\n"); return; }

    char accNum[16];
    promptExistingAccount(accNum, sizeof(accNum));

    Account a;
    if (!loadAccountFromFile(accNum, &a)) {
        printf("Error: failed to load account file for %s.\n", accNum);
        return;
    }

    char last4[8];
    printf("Enter last 4 characters of ID to confirm: ");
    readLine(last4, sizeof(last4));
    if (!isDigits(last4) || strlen(last4) != 4) {
        printf("Error: must enter exactly 4 digits.\n"); return;
    }
    int idlen = (int)strlen(a.id);
    if (idlen < 4 || strcmp(last4, a.id + idlen - 4) != 0) {
        printf("Error: ID confirmation does not match last 4 digits of registered ID.\n"); return;
    }

    char pin1[8], pin2[8];
    // --- PIN AUTHENTICATION START ---
    promptPIN(pin1, sizeof(pin1), "Enter 4-digit PIN for this account");
    if (strcmp(pin1, a.pin) != 0) { 
        printf("Error: PIN incorrect. Delete aborted.\n"); 
        return; // Exits immediately on first PIN failure
    }
    
    // --- SECOND PIN CONFIRMATION ---
    promptPIN(pin2, sizeof(pin2), "Re-enter 4-digit PIN to confirm deletion");
    if (strcmp(pin1, pin2) != 0) {
        printf("Error: PIN mismatch on confirmation. Delete aborted.\n"); return;
    }
    // --- PIN AUTHENTICATION END ---

    // confirmation yes/no
    char confirm[8];
    printf("ARE YOU SURE you want to delete account %s? THIS CANNOT BE UNDONE. (yes/no): ", accNum);
    readLine(confirm, sizeof(confirm));
    for (size_t j=0;j<strlen(confirm);++j) confirm[j]=tolower((unsigned char)confirm[j]);
    if (strcmp(confirm,"yes") != 0) {
        printf("Delete cancelled by user.\n"); return;
    }

    // remove file and index entry
    char path[256];
    snprintf(path, sizeof(path), "%s/%s.txt", DB_DIR, accNum);
    if (remove(path) != 0) {
        printf("Warning: failed to delete file %s (maybe missing). Attempting to remove index entry anyway.\n", path);
    }
    if (!removeFromIndex(accNum)) {
        printf("Warning: failed to remove account from index or it wasn't present.\n");
    }

    char logbuf[256];
    snprintf(logbuf, sizeof(logbuf), "DELETE account %s (Name: %s)", accNum, a.name);
    appendLog(logbuf);

    printf("Success: Account %s deleted and removed from records.\n", accNum);
    printProgressBar("Cleaning records...");
}

static void cmdDeposit() {
    printf("\n--- Deposit ---\n");
    char accNum[16], pin[8];
    promptExistingAccount(accNum, sizeof(accNum));
    promptPIN(pin, sizeof(pin), "Enter 4-digit PIN");

    Account a;
    if (!loadAccountFromFile(accNum, &a)) {
        printf("Error: failed to load account for %s.\n", accNum); return;
    }
    if (strcmp(pin, a.pin) != 0) {
        printf("Error: authentication failed (PIN incorrect). Deposit aborted.\n"); 
        return; 
    }

    printf("Current balance: RM%.2f\n", a.balance);
   
    double amt = promptAmount("Enter deposit amount (greater than RM0.00, max RM50,000.00)", 50000.0, true);

    a.balance += amt;
    if (!updateAccountFile(&a)) {
        printf("Error: failed to update account file.\n"); return;
    }

    char logbuf[256];
    snprintf(logbuf, sizeof(logbuf), "DEPOSIT RM%.2f to %s (NewBal: RM%.2f)", amt, accNum, a.balance);
    appendLog(logbuf);

    printf("Success: Deposited RM%.2f to account %s.\nNew balance: RM%.2f\n", amt, accNum, a.balance);
    printProgressBar("Updating account...");
}

static void cmdWithdraw() {
    printf("\n--- Withdraw ---\n");
    char accNum[16], pin[8];
    promptExistingAccount(accNum, sizeof(accNum));
    promptPIN(pin, sizeof(pin), "Enter 4-digit PIN");

    Account a;
    if (!loadAccountFromFile(accNum, &a)) {
        printf("Error: failed to load account for %s.\n", accNum); return;
    }
    if (strcmp(pin, a.pin) != 0) {
        printf("Error: authentication failed (PIN incorrect). Withdrawal aborted.\n"); 
        return; 
    }

    printf("Available balance: RM%.2f\n", a.balance);
    double amt = promptAmount("Enter withdrawal amount (greater than RM0.00)", 0.0, false);

    if (amt > a.balance) {
        printf("Error: insufficient funds. You have RM%.2f available.\n", a.balance);
        return;
    }

    a.balance -= amt;
    if (!updateAccountFile(&a)) {
        printf("Error: failed to update account file after withdrawal.\n"); return;
    }

    char logbuf[256];
    snprintf(logbuf, sizeof(logbuf), "WITHDRAW RM%.2f from %s (NewBal: RM%.2f)", amt, accNum, a.balance);
    appendLog(logbuf);

    printf("Success: Withdrawn RM%.2f from account %s.\nNew balance: RM%.2f\n", amt, accNum, a.balance);
    printProgressBar("Processing withdrawal...");
}

static void cmdRemit() {
    printf("\n--- Remittance / Transfer ---\n");
    // ask sender name for extra check
    char senderName[100];
    printf("Sender full name (for verification): ");
    readLine(senderName, sizeof(senderName));
    if (strlen(senderName) == 0) { printf("Error: name cannot be empty.\n"); return; }

    char fromAcc[16];
    promptExistingAccount(fromAcc, sizeof(fromAcc));

    char pin[8];
    promptPIN(pin, sizeof(pin), "Enter sender 4-digit PIN");

    Account from;
    if (!loadAccountFromFile(fromAcc, &from)) { printf("Error: failed to load sender account.\n"); return; }
    if (strcmp(pin, from.pin) != 0) { 
        printf("Error: authentication failed (PIN incorrect). Remittance aborted.\n"); 
        return; 
    }
    if (!strCaseEqual(senderName, from.name)) { printf("Error: provided name does not match account name on file.\n"); return; }

    char toAcc[16];
    printf("Receiver account number: ");
    readLine(toAcc, sizeof(toAcc));
    if (!isDigits(toAcc) || strlen(toAcc) < 7 || strlen(toAcc) > 9) { printf("Error: invalid receiver account format.\n"); return; }
    if (!accountExists(toAcc)) { printf("Error: receiver account %s not found.\n", toAcc); return; }
    if (strcmp(toAcc, fromAcc) == 0) { printf("Error: sender and receiver must be different accounts.\n"); return; }

    Account to;
    if (!loadAccountFromFile(toAcc, &to)) { printf("Error: failed to load receiver account.\n"); return; }

    double amt = promptAmount("Enter transfer amount (greater than RM0.00)", 0.0, false);
    // apply fee rules:
    double fee = 0.0;
    if (strcmp(from.type, "savings") == 0 && strcmp(to.type, "current") == 0) fee = amt * 0.02;
    else if (strcmp(from.type, "current") == 0 && strcmp(to.type, "savings") == 0) fee = amt * 0.03;
    // ensure available balance covers amt + fee
    if (amt + fee > from.balance) {
        printf("Error: insufficient funds. Transfer (%0.2f) + fee (%0.2f) exceeds your balance RM%.2f.\n", amt, fee, from.balance);
        return;
    }

    from.balance -= (amt + fee);
    to.balance += amt;

    if (!updateAccountFile(&from) || !updateAccountFile(&to)) {
        printf("Error: failed to update account file(s) after remittance. Aborting.\n"); return;
    }

    char logbuf[256];
    snprintf(logbuf, sizeof(logbuf), "REMIT RM%.2f from %s to %s (Fee: RM%.2f) SenderNewBal: RM%.2f", amt, fromAcc, toAcc, fee, from.balance);
    appendLog(logbuf);

    printf("Success: Sent RM%.2f from %s to %s.\n", amt, fromAcc, toAcc);
    if (fee > 0.0) printf("Fee applied: RM%.2f\n", fee);
    printf("Sender new balance: RM%.2f\n", from.balance);
    printProgressBar("Transferring funds...");
}

static void cmdHelp() {
    printf("\n--- Help & Support ---\n");
    printf("What are you looking for?\n");
    printf("1. How to create an account\n");
    printf("2. How to deposit/withdraw\n");
    printf("3. How remittance works and fees\n");
    printf("4. Contact/Report an issue (send request)\n");
    printf("Enter choice or 'back' to return: ");
    char choice[32];
    readLine(choice, sizeof(choice));
    if (strcmp(choice, "1") == 0) {
        printf("\nCreate account: choose 'Create Account' from menu, then provide Name, 7-digit ID, account type (savings/current), 4-digit PIN. Account number will be generated.\n");
    } else if (strcmp(choice, "2") == 0) {
        printf("\nDeposit/Withdraw: choose deposit or withdraw, authenticate with account number and PIN. Deposit allowed > RM0 and ≤ RM50,000 per operation.\n");
    } else if (strcmp(choice, "3") == 0) {
        printf("\nRemittance: sender authenticates with PIN. Savings->Current: 2%% fee. Current->Savings: 3%% fee. Fee deducted from sender.\n");
    } else if (strcmp(choice, "4") == 0) {
        printf("\nSend a help request. Enter your email or phone to be notified (saved locally for now).\n");
        char contact[128], issue[256];
        printf("Enter your email or phone: "); readLine(contact, sizeof(contact));
        printf("Briefly describe the issue: "); readLine(issue, sizeof(issue));
        FILE *f = fopen(HELP_REQ_FILE, "a");
        if (f) {
            time_t t = time(NULL);
            char tb[64];
            struct tm *tm = localtime(&t);
            strftime(tb, sizeof(tb), "%Y-%m-%d %H:%M:%S", tm);
            fprintf(f, "[%s] %s | %s\n", tb, contact, issue);
            fclose(f);
            printf("Request received. We'll notify you at %s (saved locally).\n", contact);
            appendLog("Help request submitted");
        } else printf("Error: failed to save help request.\n");
    } else {
        printf("Returning to main menu.\n");
    }
}

/* ---------- Menu & session ---------- */
static void printHeader() {
    printf("=============================================\n");
    printf("   Welcome to Krish Enterprise Bank\n");
    printf("   How may I help you today?\n");
    printf("=============================================\n");
    time_t t = time(NULL);
    char tb[64];
    struct tm *tm = localtime(&t);
    strftime(tb, sizeof(tb), "%Y-%m-%d %H:%M:%S", tm);
    printf("Session started: %s\n", tb);
    printf("Loaded accounts: %d\n", countAccounts());
    printf("---------------------------------------------\n");
}

int main(void) {
    ensureDatabase();
    char input[64];

    printHeader();

    for (;;) {
        printf("\nMENU: (type number or keyword)\n");
        printf("1) Create        (create)\n");
        printf("2) Delete        (delete)\n");
        printf("3) Deposit       (deposit)\n");
        printf("4) Withdraw      (withdraw)\n");
        printf("5) Remittance    (remit / remittance)\n");
        printf("6) Help          (help)\n");
        printf("7) Exit          (exit)\n");
        printf("Select option: ");
        readLine(input, sizeof(input));

        for (size_t i=0;i<strlen(input);++i) input[i]=tolower((unsigned char)input[i]);
        if (strcmp(input, "1")==0 || strcmp(input,"create")==0) {
            cmdCreate();
        } else if (strcmp(input,"2")==0 || strcmp(input,"delete")==0) {
            cmdDelete();
        } else if (strcmp(input,"3")==0 || strcmp(input,"deposit")==0) {
            cmdDeposit();
        } else if (strcmp(input,"4")==0 || strcmp(input,"withdraw")==0) {
            cmdWithdraw();
        } else if (strcmp(input,"5")==0 || strcmp(input,"remit")==0 || strcmp(input,"remittance")==0) {
            cmdRemit();
        } else if (strcmp(input,"6")==0 || strcmp(input,"help")==0) {
            cmdHelp();
        } else if (strcmp(input,"7")==0 || strcmp(input,"exit")==0 || strcmp(input,"quit")==0) {
            printf("Thank you for using Krish Enterprise Bank. Goodbye!\n");
            break;
        } else {
            printf("Invalid option. Please enter a menu number or keyword (e.g., 'create', 'deposit', 'remit', 'help', 'exit').\n");
        }
    }

    return 0;
}