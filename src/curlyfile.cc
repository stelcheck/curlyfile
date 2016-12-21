#include "curlyfile.h"

using v8::FunctionTemplate;

NAN_MODULE_INIT(InitAll) {
  Curlyfile::Init(target);
}

void CurlyfileAsyncWorker::Execute() {
  CURLcode code;
  long httpCode = 0;
  error[0] = '\0';

  FILE *file = fopen(outfile, "wb");
  if (!file) {
    sprintf(error, "Failed to open file");
    return;
  }

  curl_easy_setopt(session, CURLOPT_URL, url);
  curl_easy_setopt(session, CURLOPT_ERRORBUFFER, error);
  // curl_easy_setopt(session, CURLOPT_WRITEFUNCTION, write_data);
  curl_easy_setopt(session, CURLOPT_PRIVATE, file);
  curl_easy_setopt(session, CURLOPT_WRITEDATA, file);
  code = curl_easy_perform(session);

  if (code != CURLE_OK) {
    sprintf(error, "CURL Failed (%s)", curl_easy_strerror(code));
  } else {
    curl_easy_getinfo(session, CURLINFO_RESPONSE_CODE, &httpCode);
    if (httpCode != 200) {
      sprintf(error, "Non-200 response (received %ld)", httpCode);
    }
  }

  fclose(file);
  // curly->sessions.push_back(session);
}

void CurlyfileAsyncWorker::HandleOKCallback() {
  v8::Local<v8::Value> argv[1];

  if (strlen(error) > 0) {
    argv[0] = Nan::Error(error);
  } else {
    argv[0] = Nan::Undefined();
  }

  callback->Call(1, argv);
}

Nan::Persistent<v8::Function> Curlyfile::constructor;

NAN_MODULE_INIT(Curlyfile::Init) {
  curl_global_init(CURL_GLOBAL_ALL);

  downloader_init();

  v8::Local<v8::FunctionTemplate> tpl = Nan::New<v8::FunctionTemplate>(New);
  tpl->SetClassName(Nan::New("Curlyfile").ToLocalChecked());
  tpl->InstanceTemplate()->SetInternalFieldCount(1);

  Nan::SetPrototypeMethod(tpl, "download", Download);

  constructor.Reset(Nan::GetFunction(tpl).ToLocalChecked());
  Nan::Set(target, Nan::New("Curlyfile").ToLocalChecked(), Nan::GetFunction(tpl).ToLocalChecked());
}

size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
  size_t written = fwrite(ptr, size, nmemb, stream);
  return written;
}

Curlyfile::Curlyfile() {
  for (int i = 0; i < MAX_CONCURRENT; i++){
    CURL *session = curl_easy_init();

    curl_easy_setopt(session, CURLOPT_WRITEFUNCTION, write_data);
    curl_easy_setopt(session, CURLOPT_MAXCONNECTS, 1);
    curl_easy_setopt(session, CURLOPT_TCP_KEEPALIVE, 1);
    curl_easy_setopt(session, CURLOPT_TCP_KEEPIDLE, 30L);
    curl_easy_setopt(session, CURLOPT_TCP_KEEPINTVL, 5L);
    curl_easy_setopt(session, CURLOPT_DNS_CACHE_TIMEOUT, 300);
    // curl_easy_setopt(session, CURLOPT_PIPEWAIT, 1L);

    DownloadObject *download = new DownloadObject(this, session);
    curl_easy_setopt(session, CURLOPT_ERRORBUFFER, &(download->error));
    curl_easy_setopt(session, CURLOPT_PRIVATE, download);

    downloads.push_back(download);
  }
}

Curlyfile::~Curlyfile() {
  for (int i = 0; i < MAX_CONCURRENT; i++){
    DownloadObject *download = downloads.back();
    curl_easy_cleanup(download->session);
    downloads.pop_back();
  }
}

NAN_METHOD(Curlyfile::New) {
  if (info.IsConstructCall()) {
    Curlyfile *obj = new Curlyfile();
    obj->Wrap(info.This());
    info.GetReturnValue().Set(info.This());
  }
}

char* ToCString(v8::Local<v8::Value> value) {
  if (value->IsString()) {
    v8::String::Utf8Value string(value);
    char *str = (char *) malloc(string.length() + 1);
    strcpy(str, *string);
    return str;
  }

  return NULL;
}

NAN_METHOD(Curlyfile::Download) {
  Isolate* isolate = Isolate::GetCurrent();
  HandleScope scope(isolate);

  if (info.Length() < 3) {
    isolate->ThrowException(Exception::TypeError(
          String::NewFromUtf8(isolate, "Wrong number of arguments")));
    return;
  }

  if (!info[0]->IsString() || !info[1]->IsString()) {
    isolate->ThrowException(Exception::TypeError(
          String::NewFromUtf8(isolate, "Wrong arguments")));
    return;
  }

  Curlyfile *curly = Nan::ObjectWrap::Unwrap<Curlyfile>(info.This());
  char *url = ToCString(info[0]);
  char *outfile = ToCString(info[1]);

  DownloadObject *download = curly->downloads.front();
  curly->downloads.pop_front();

  download->file = fopen(outfile, "wb");
  if (!download->file) {
    // todo: callback instead
    isolate->ThrowException(Exception::TypeError(
          String::NewFromUtf8(isolate, "Failed to open file")));
    return;
  }

  download->callback = new Callback(info[2].As<v8::Function>());

  curl_easy_setopt(download->session, CURLOPT_URL, url);
  curl_easy_setopt(download->session, CURLOPT_WRITEDATA, download->file);


  add_download(download->session);
  // CurlyfileAsyncWorker* worker = new CurlyfileAsyncWorker(
  //     callback,
  //     curly,
  //     session,
  //     url,
  //     outfilename
  //     );

  // Nan::AsyncQueueWorker(worker);
}

NODE_MODULE(curlyfile, InitAll)