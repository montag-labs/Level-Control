#ifndef NETWORK_WORKER_H
#define NETWORK_WORKER_H

// Eigener FreeRTOS-Task fuer alles, was lange blockieren kann: MQTT (Verbindungsaufbau, Zustandsmeldungen),
// SMTP (Schwellwert-Mails, Test-Mail, Diagnose). So halten diese Netzwerkzugriffe weder die Messung in der
// Hauptschleife noch den Webserver auf. MQTT und SMTP werden ausschliesslich aus diesem Task bedient;
// andere Tasks stellen nur Anfragen (MqttManager::requestReconnect, PushNotificationManager::request*).
namespace NetworkWorker {

void start();

}  // namespace NetworkWorker

#endif  // NETWORK_WORKER_H
