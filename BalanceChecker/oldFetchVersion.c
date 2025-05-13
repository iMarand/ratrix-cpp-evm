#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>

struct string {
    char *ptr;
    size_t len;
};


void init_string(struct string *s) {
    s->len = 0;
    s->ptr = malloc(s->len+1);
    if (s->ptr == NULL) {
        fprintf(stderr, "malloc() failed\n");
        exit(EXIT_FAILURE);
    }
    s->ptr[0] = '\0';
}

size_t writefunc(void *ptr, size_t size, size_t nmemb, struct string *s)
{
    size_t new_len = s->len + size*nmemb;
    s->ptr = realloc(s->ptr, new_len+1);
    if (s->ptr == NULL) {
        fprintf(stderr, "realloc() failed\n");
        exit(EXIT_FAILURE);
    }
    memcpy(s->ptr+s->len, ptr, size*nmemb);
    s->ptr[new_len] = '\0';
    s->len = new_len;

    return size*nmemb;
}

int main(void)
{
    CURL *curl;
    CURLcode res;
    struct string s;
    init_string(&s);
    
    const char *apiKey = "ArVnfIzM1EUmjlTAfgTZz37pq0UtTSsR77QnvmJKmvm9";
    const char *address = "0x1972633C2BE10c4B977d3260Fee1cFf228bf2a5d";
    const char *endpoint = "lb.drpc.org/ogrpc?network=ethereum&dkey=ArVnfIzM1EUmjlTAfgTZz37pq0UtTSsR77QnvmJKmvm9/";

    char postfields[256];
    snprintf(postfields, sizeof(postfields), 
             "{\"jsonrpc\":\"2.0\",\"method\":\"eth_getBalance\",\"params\":[\"%s\",\"latest\"],\"id\":1}", 
             address);

    curl = curl_easy_init();
    if(curl) {
        struct curl_slist *headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        
        char auth_header[256];
        snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", apiKey);
        headers = curl_slist_append(headers, auth_header);

        curl_easy_setopt(curl, CURLOPT_URL, endpoint);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postfields);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writefunc);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);

        // Enable following redirects
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        
        // Optionally, set a maximum number of redirects (e.g., 5)
        curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);

        res = curl_easy_perform(curl);
        if(res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n",
                    curl_easy_strerror(res));
        else {
            char *effective_url = NULL;
            curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url);
            if (effective_url) {
                printf("Effective URL: %s\n", "url");
            }
        }

        printf("Response: %s\n", s.ptr);
        char ethG[256];
        
        printf("%s", ethG);
        

        free(s.ptr);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    
    
    return 0;
}
