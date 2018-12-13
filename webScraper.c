#define PCRE2_CODE_UNIT_WIDTH 8

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <gumbo.h>
#include <pcre2.h>


typedef struct gumboStackNode
{
    GumboNode *node;
    struct gumboStackNode *next;
} gumboStackNode;

typedef struct urlStackNode
{
    struct urlStackNode *next;
    char *URL;
    int urlLength;
} urlStackNode;

typedef struct ScrapingInfo
{
    char *baseURL;
    char *pathURL;
    char *originalURL;
    char *IP;
} ScrapingInfo;

ScrapingInfo *parsedInfo;
urlStackNode *TotalUrlStack;

pthread_mutex_t urlAddMutex = PTHREAD_MUTEX_INITIALIZER;

void pushGumboStackNode(gumboStackNode **head, GumboNode *node)
{
    if (!(*head))
    {
        *head = malloc(sizeof(gumboStackNode));

        (*head)->node = node;
        (*head)->next = NULL;

        return;
    }

    gumboStackNode *currentHead = *head;

    gumboStackNode *newHead = malloc(sizeof(gumboStackNode));

    newHead->node = node;
    newHead->next = currentHead;

    *head = newHead;
}

GumboNode *popGumboStackNode(gumboStackNode **head)
{
    if (!(*head))
    {
        return NULL;
    }

    gumboStackNode *currentHead = *head;

    if (currentHead->next)
        *head = currentHead->next;
    else
        *head = NULL;

    GumboNode *poppedValue = currentHead->node;

    free(currentHead);

    return poppedValue;
}

void pushURLStackNode(urlStackNode **head, char *URL)
{
    if (!(*head))
    {
        *head = malloc(sizeof(urlStackNode));

        (*head)->URL = URL;
        (*head)->next = NULL;

        return;
    }

    urlStackNode *currentHead = *head;

    urlStackNode *newHead = malloc(sizeof(urlStackNode));

    newHead->URL = URL;
    newHead->next = currentHead;

    *head = newHead;
}

char *popURLStackNode(urlStackNode **head)
{
    if (!(*head))
    {
        return NULL;
    }

    urlStackNode *currentHead = *head;

    if (currentHead->next)
        *head = currentHead->next;
    else
        *head = NULL;

    char *poppedValue = currentHead->URL;

    free(currentHead);

    return poppedValue;
}

// If the pointer returned is NULL, then errorCode will indicate which error occured,
// otherwise it will indicate the number of URLs found
char **getURLs(char *HTML, int *errorCode)
{
    char *copiedURL, **returnURLs = NULL;
    int i, lengthOfURL, returnURLsLength = 0;

    *errorCode = 0;

    GumboOutput *parsedOutput = gumbo_parse(HTML);

    if (!parsedOutput)
    {
        // Indicate the parsing failed with error code 1
        *errorCode = 1;

        return NULL;
    }

    gumboStackNode *stack = NULL;
    urlStackNode *urlStack = NULL;

    GumboAttribute *href;
    GumboNode *node;

    pushGumboStackNode(&stack, parsedOutput->root);

    while (stack)
    {
        node = popGumboStackNode(&stack);

        if (node->v.element.tag == GUMBO_TAG_A)
        {
            href = gumbo_get_attribute(&node->v.element.attributes, "href");

            if (href)
            {
                lengthOfURL = strlen(href->value);

                // +1 to account for NULL terminator
                copiedURL = malloc(sizeof(char) * (lengthOfURL + 1));

                strncpy(copiedURL, href->value, lengthOfURL);
                copiedURL[lengthOfURL] = 0;

                pushURLStackNode(&urlStack, copiedURL);

                returnURLsLength++;
            }
        }

        GumboVector *children = &node->v.element.children;

        for (i = 0; i < children->length; ++i)
        {
            node = (GumboNode *)children->data[i];

            if (node->type == GUMBO_NODE_ELEMENT)
                pushGumboStackNode(&stack, node);
        }
    }

    if (returnURLsLength)
    {
        returnURLs = malloc(sizeof(char *) * returnURLsLength);

        for (i = 0; i < returnURLsLength; i++)
            returnURLs[i] = popURLStackNode(&urlStack);
    }

    *errorCode = returnURLsLength;

    return returnURLs;
}

void freeScrapingInfo(ScrapingInfo *scrapingInfo)
{
    if (scrapingInfo)
    {
        if (scrapingInfo->baseURL)
            free(scrapingInfo->baseURL);

        if (scrapingInfo->IP)
            free(scrapingInfo->IP);

        if (scrapingInfo->originalURL)
            free(scrapingInfo->originalURL);

        if (scrapingInfo->pathURL)
            free(scrapingInfo->pathURL);

        free(scrapingInfo);
    }
}

ScrapingInfo *getScrapingInfo(char *originalURL, int *errorCode)
{
    // Current raw regex pattern: "(?:.*?\/\/)?((?:www\.)?.*?(?:\.\w+)+)(\/.*)?"

    // Uninitialized compiled regex variable
    pcre2_code *re;
    // Offset array for the error offsets
    PCRE2_SIZE erroroffset;

    // PCRE2 String Pointer Type
    PCRE2_SPTR name_table, subject = (PCRE2_SPTR)originalURL, pattern = (PCRE2_SPTR) "(?:.*?\\/\\/)?((?:www\\.)?.*?(?:\\.\\w+)+)(\\/.*)?";

    // Dummy error code catcher, we need it to call the function, won't use it later
    int errorNumber;

    // String length of the original URL
    size_t subject_length = strlen(originalURL);

    // Compile the regex pattern
    // pcre2_compile(pattern, pattern is NULL terminated, default options, error variable, error offset variable, use default compile context)
    re = pcre2_compile(pattern,
                       PCRE2_ZERO_TERMINATED,
                       0,
                       &errorNumber,
                       &erroroffset,
                       NULL);

    // An error occured if the pointer returned is NULL
    if (re == NULL)
    {
        // Set error code to 1, indicating that compilation of the regex failed
        *errorCode = 1;

        return NULL;
    }

    // Get match data from the compiled regex
    pcre2_match_data *matchData = pcre2_match_data_create_from_pattern(re, NULL);

    // Try to get a match, matchResult will indicate success or failure
    // pcre2_match(compiled pattern, string to match against (SMA), length of SMA, offset of matching start in SMA, options (0 means default), result data storage location, match context (NULL means default))
    int matchResult = pcre2_match(re, subject, subject_length, 0, 0, matchData, NULL);

    // An error occured if matchResult is <= 0
    if (matchResult <= 0)
    {
        // Error code 2, indicating the regex matching failed
        *errorCode = 2;

        // Release the memory used for the match
        pcre2_match_data_free(matchData);

        // Release the memory for the compiled regex pattern
        pcre2_code_free(re);

        return NULL;
    }

    // Allocate memory for the ScrapingInfo struct that will be returned
    ScrapingInfo *parsedInfo = (ScrapingInfo *)malloc(sizeof(ScrapingInfo));

    // String lengths for the original string, base URL match, and path portion of the URL match
    size_t stringLengths[3];

    // Create an offset vector from the match
    PCRE2_SIZE *matchOffsetArray = pcre2_get_ovector_pointer(matchData);

    // Populate our string lengths so we can allocate memory properly
    for (int i = 0; i < matchResult; i++)
        stringLengths[i] = matchOffsetArray[2 * i + 1] - matchOffsetArray[2 * i];

    // Allocate memory for the fields of the returned ScrapingInfo structure
    parsedInfo->originalURL = (char *)malloc(sizeof(char) * stringLengths[0] + 1);
    parsedInfo->baseURL = (char *)malloc(sizeof(char) * stringLengths[1] + 1);

    // Copy the matches to their respective fields of the ScrapingInfo structure
    strncpy(parsedInfo->originalURL, subject + matchOffsetArray[0], stringLengths[0]);
    strncpy(parsedInfo->baseURL, subject + matchOffsetArray[2], stringLengths[1]);

    // Handles the case where there is no path at the end of the URL
    if (matchResult == 3)
    {
        parsedInfo->pathURL = (char *)malloc(sizeof(char) * stringLengths[2] + 1);
        strncpy(parsedInfo->pathURL, subject + matchOffsetArray[4], stringLengths[2]);
    }
    else
    {
        // sizeof(char) * 2 needed for '/' + NULL terminator
        parsedInfo->pathURL = (char *)malloc(sizeof(char) * 2);
        parsedInfo->pathURL[0] = '/';
        stringLengths[2] = 1;
    }

    // Set the NULL terminators for each of our fields
    parsedInfo->originalURL[stringLengths[0]] = 0;
    parsedInfo->baseURL[stringLengths[1]] = 0;
    parsedInfo->pathURL[stringLengths[2]] = 0;

    // Release the memory used for the match
    pcre2_match_data_free(matchData);

    // Release the memory for the compiled regex pattern
    pcre2_code_free(re);

    *errorCode = 0;

    struct hostent *hostEntry;

    // To retrieve host information
    hostEntry = gethostbyname(parsedInfo->baseURL);

    // NULL indicates that 'gethostbyname' failed and the URL was inaccessible
    if (hostEntry == NULL)
    {
        // Error code 2, indicating the regex matching failed
        *errorCode = 3;

        return NULL;
    }

    // To convert an IP address to its string representation
    // parsedInfo->IP = inet_ntoa(*((struct in_addr *)hostEntry->h_addr_list[0]));
    int ipLength = strlen(hostEntry->h_addr_list[0]);

    parsedInfo->IP = malloc(sizeof(char) * (ipLength + 1));

    // +1 to include space for the full IP string and the NULL terminator
    strncpy(parsedInfo->IP, hostEntry->h_addr_list[0], ipLength);
    parsedInfo->IP[ipLength] = 0;

    return parsedInfo;
}

char *makeHTTPRequest(char *IP, char *baseURL, char *pathURL, int bufferSize, int *errorCode)
{
    int socketFileDesc = socket(AF_INET, SOCK_STREAM, 0);

    // socket() will indicate failure with -1
    if (socketFileDesc == -1)
    {
        // Indicate failure to create a socket with error code 1
        *errorCode = 1;

        // Perform cleanup
        close(socketFileDesc);

        return NULL;
    }

    // serverAddress is the struct representing the server to connect to
    struct sockaddr_in serv_addr;

    // Zero out the memory for the serverAddress struct
    bzero((char *)&serv_addr, sizeof(serv_addr));

    // Set TCP connection
    serv_addr.sin_family = AF_INET;

    // Copy IP to server address field
    bcopy((char *)IP,
          (char *)&serv_addr.sin_addr.s_addr,
          strlen(IP));

    // Converts port to network byte order
    serv_addr.sin_port = htons(80);

    // Try to connect to server
    int socketStatus = connect(socketFileDesc, (struct sockaddr *)&serv_addr, sizeof(serv_addr));

    // If socketStatus < 0 an error occured
    if (socketStatus < 0)
    {
        // Indicate connection failure with error code 2
        *errorCode = 2;

        // Perform cleanup
        close(socketFileDesc);

        return NULL;
    }

    char *requestFormatString = "GET %s HTTP/1.1\r\nHost: %s\r\nUser-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_9_3) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/35.0.1916.47 Safari/537.36\r\n\r\n";

    // Calculate length needed for the request body string,
    // subtract 4 to account for the space taken up by format specifiers
    int totalLength = strlen(requestFormatString) + strlen(baseURL) + strlen(pathURL) - 4 + 1;

    // Create string to hold request body, add 1 for the NULL terminator
    char requestString[totalLength];

    // Create request body
    snprintf(requestString, totalLength, requestFormatString, pathURL, baseURL);

    // Create a buffer for holding the read data + NULL terminator
    char streamBuffer[bufferSize + 1];

    // Create buffer for accumulating read in data to find the end of the headers
    // and beginning of response body, and create buffer to hold the response body itself
    char *responseHeadersBuffer = NULL, *responseBodyBuffer = NULL;

    int numberOfBytesWritten = write(socketFileDesc, requestString, strlen(requestString));

    // If the returned number of bytes written is less than 0 an error occured
    if (numberOfBytesWritten < 0)
    {
        // Indicate connection write failure with error code 3
        *errorCode = 3;

        close(socketFileDesc);

        return NULL;
    }

    int responseBodyBufferLength = 0, preamble = 1, preambleBufferLength = 0, statusCode = 0;

    // int numberOfBytesRead = read(socketFileDesc, streamBuffer, bufferSize);
    int numberOfBytesRead = recv(socketFileDesc, streamBuffer, bufferSize, 0);

    do
    {
        if (numberOfBytesRead < 0)
        {
            // Indicate connection read failure with error code 4
            *errorCode = 4;

            // Perform cleanup
            if (responseBodyBuffer)
                free(responseBodyBuffer);

            if (responseHeadersBuffer)
                free(responseHeadersBuffer);

            close(socketFileDesc);

            return NULL;
        }

        streamBuffer[numberOfBytesRead] = 0;

        if (preamble)
        {
            if (responseHeadersBuffer == NULL)
            {
                preambleBufferLength = numberOfBytesRead;

                // +1 to account for NULL terminator
                responseHeadersBuffer = (char *)malloc(sizeof(char) * (preambleBufferLength + 1));

                strncpy(responseHeadersBuffer, streamBuffer, preambleBufferLength);
                responseHeadersBuffer[preambleBufferLength] = 0;
            }
            else
            {
                preambleBufferLength += numberOfBytesRead;

                responseHeadersBuffer = realloc(responseHeadersBuffer, sizeof(char) * (preambleBufferLength + 1));

                strncat(responseHeadersBuffer, streamBuffer, numberOfBytesRead);
                responseHeadersBuffer[preambleBufferLength] = 0;
            }

            /* Stack Smashing not occuring in here; check responseBodyBuffer */

            // Points to the memory location where the beginning of the substring "\r\n\r\n" occurs
            char *headerEndPointer = strstr(responseHeadersBuffer, "\r\n\r\n");

            // headerEndPointer will be NULL if the substring cannot be found
            if (headerEndPointer)
            {
                // Some pointer arithmatic with the found pointer to get the starting index of the substring
                int headerEndingLength = headerEndPointer - responseHeadersBuffer;

                // Need to subtract 4 more to headerEndingLength account for the '\r\n\r\n' substring
                responseBodyBufferLength = (preambleBufferLength - headerEndingLength - 4);

                // +1 to account for NULL terminator
                responseBodyBuffer = (char *)malloc(sizeof(char) * (responseBodyBufferLength + 1));

                // 4 to skip the '\r\n\r\n' substring
                strncpy(responseBodyBuffer, (char *)headerEndPointer + 4, responseBodyBufferLength);

                responseBodyBuffer[responseBodyBufferLength] = 0;

                //printf("%d %d %d %d\n", strlen(headerEndPointer + 4), responseBodyBufferLength, strlen(responseBodyBuffer), numberOfBytesRead);

                preamble = 0;
            }

            // Status code should be found on first line, function will assume failure if  (buffer should be set to a reasonable length)
            if (!statusCode)
            {
                // Attempt to parse status code
                int charactersWritten = sscanf(responseHeadersBuffer, "%*s %d", &statusCode);

                // charactersWritten < 0 indicates failure to parse status code, possible a malformed response;
                // however, the failure is fine in the case that the buffer is small and not enough of the response
                // has been read to get the status code
                if (charactersWritten < 0)
                    statusCode = 0;
            }
        }
        else
        {
            responseBodyBufferLength += numberOfBytesRead;

            responseBodyBuffer = (char *)realloc(responseBodyBuffer, sizeof(char) * (responseBodyBufferLength + 1));
            strncat(responseBodyBuffer, streamBuffer, numberOfBytesRead);

            responseBodyBuffer[responseBodyBufferLength] = 0;
        }

        if (statusCode && (statusCode < 200 || statusCode > 299))
        {
            // Indicate bad response (non-200 status code) with error code 5
            *errorCode = 5;

            // int l1 = strlen(responseBodyBuffer), l2 = strlen(responseHeadersBuffer), l3 = strlen(streamBuffer);

            // puts(responseBodyBuffer);

            // printf("%d %d %ld, %d %d %ld, %d %d %ld\n", l1, responseBodyBufferLength, (&responseBodyBuffer)[1] - responseBodyBuffer, l2, preambleBufferLength, (&responseHeadersBuffer)[1] - responseHeadersBuffer, l3, numberOfBytesRead, (&streamBuffer)[1] - streamBuffer);

            // Perform cleanup
            if (responseBodyBuffer)
                free(responseBodyBuffer);

            if (responseHeadersBuffer)
                free(responseHeadersBuffer);

            close(socketFileDesc);

            return NULL;
        }
        // } while ((numberOfBytesRead = read(socketFileDesc, streamBuffer, bufferSize)));
    } while ((numberOfBytesRead = recv(socketFileDesc, streamBuffer, bufferSize, 0)));

    if (streamBuffer)
        free(responseHeadersBuffer);

    close(socketFileDesc);

    *errorCode = 0;

    return responseBodyBuffer;
}

void *scrapingOperations(void *path)
{
    int errorCode;
    char *scrapePath = (char *)path;

    char *responseBody = makeHTTPRequest(parsedInfo->IP, parsedInfo->baseURL, scrapePath, 4096, &errorCode);

    if (!responseBody)
    {
        printf("Could not get HTML for the provided URL, ");

        if (errorCode == 1)
            printf("creating a socket to make the initial request!\n");
        else if (errorCode == 2)
            printf("connection failure!\n");
        else if (errorCode == 3)
            printf("connection write failure!\n");
        else if (errorCode == 4)
            printf("connection read failure!\n");
        else if (errorCode == 5)
            printf("bad response (non-200 status code)!\n");

        free(responseBody);
        freeScrapingInfo(parsedInfo);

        return 1;
    }

    int numberOfURLsReturned = 0;

    char **URLs = getURLs(responseBody, &errorCode);

    if (URLs)
    {
        numberOfURLsReturned = errorCode;

        pthread_mutex_lock(&urlAddMutex);

        for (int i = 0; i < numberOfURLsReturned; i++) {
            printf("%s\n", URLs[i]);
            pushURLStackNode(TotalUrlStack, parsedInfo->pathURL);
        }

        pthread_mutex_unlock(&urlAddMutex);
    }

    free(responseBody);

    // Need to do proper freeing in the main thread
    /* if (numberOfURLsReturned)
    {
        for (int i = 0; i < numberOfURLsReturned; i++)
            free(URLs[i]);
    }*/

    pthread_exit(NULL);
}

int main(int argc, char **argv)
{
    char *endptr = NULL;

    int nThreads = (int) strtol(argv[1], &endptr, 10);

    if (argv[1] == endptr) {
        printf("An error occured parsing the desired number of threads.\n");

        return 1;
    } else if (nThreads <= 0) {
        printf("Number of desired threads must be > 0!\n");

        return 1;
    }

    if (argc < 2)
    {
        printf("URL must be provided as a command line argument!\n");

        return 1;
    }

    int errorCode;

    parsedInfo = getScrapingInfo(argv[2], &errorCode);

    // If NULL is returned an error occured
    if (!parsedInfo)
    {
        if (errorCode == 1)
            printf("Regex compilation failed!\n");
        else if (errorCode == 2)
            printf("Regex matching failed!\n");
        else if (errorCode == 3)
            printf("URL was inaccessable!\n");

        return 1;
    }

    int threadCounter, joinCounter;

    char *pathPointer;

    TotalUrlStack = NULL;

    pthread_t threads[nThreads];

    pushURLStackNode(TotalUrlStack, parsedInfo->pathURL);

    while (TotalUrlStack)
    {
        for (threadCounter = 0; threadCounter < nThreads && TotalUrlStack; threadCounter++)
        {
            pathPointer = popURLStackNode(TotalUrlStack);

            pthread_create(&threads[threadCounter], NULL, scrapingOperations, (void *)pathPointer);
        }

        for (joinCounter = 0; threadCounter < threadCounter; threadCounter++)
        {
            pthread_join(&threads[threadCounter], NULL);
        }
    }

    return 0;
}