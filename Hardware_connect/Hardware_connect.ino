#include <ArduinoJson.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

#define SOLENOID_VALVE D7
#define BUTTON_PIN D2
#define FLOW_SENSOR_PIN D5

#define PULSES_PER_TURN 5  // Adjust based on your sensor

// Add debounce variables
volatile unsigned long lastPulseTime = 0;
const unsigned long DEBOUNCE_TIME = 100; // 100ms debounce

volatile int pulseCount = 0;
volatile int turnCount = 0;  // Will be updated from server
bool valveOpen = false;
bool buttonState = false;
bool lastButtonState = false;
bool wasValveOpen = false;  // Track previous valve state
bool hasSentUsage = false;  // Track if we've sent usage for current session
unsigned long lastWebCheck = 0;
const unsigned long WEB_CHECK_INTERVAL = 1000; // Check web status every 1 second


float usageAmount = 0.0;  // Track water usage in cubic meters

const String userId = "1";
const String counterId = "WMS12928";  // Your specific counter identifier
const char* serverAddress = "192.168.1.112";
const int serverPort = 5000;

// Status update timing
unsigned long lastStatusUpdate = 0;
const unsigned long STATUS_UPDATE_INTERVAL = 5000; // Update every 5 seconds

// Function prototypes (forward declarations)
void checkBalance();
void checkForNewPayment();
void checkButton();
void borrowWater();
void checkFlowSensor();
bool updateValveStatus(String status, int maxRetries = 3);
void openValve();
void closeValve();
void reportWaterUsage();
void reportStatus();
void testHttpConnection();
void resetDeviceState();
void registerCounter();

void IRAM_ATTR countPulse() {
    // Debounce the flow sensor
    unsigned long currentTime = millis();
    if (currentTime - lastPulseTime < DEBOUNCE_TIME) {
        return; // Ignore pulses that come too quickly (likely noise)
    }
    lastPulseTime = currentTime;
   
    // Only count pulses if the valve is actually open
    if (valveOpen) {
        pulseCount++;
        if (pulseCount >= PULSES_PER_TURN) {  
            // Don't let turns go below zero
            if (turnCount > 0) {
                turnCount--;  // Decrease available turns when water flows
                pulseCount = 0;  
                usageAmount += 0.001;  // 0.001 cubic meters per turn
                Serial.printf("Turn completed. Usage: %.3f m³, Remaining turns: %d\n", usageAmount, turnCount);
            }
        }
    }
}

void setup() {
    pinMode(SOLENOID_VALVE, OUTPUT);
    pinMode(BUTTON_PIN, INPUT_PULLUP);
    pinMode(FLOW_SENSOR_PIN, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), countPulse, RISING);
   
    digitalWrite(SOLENOID_VALVE, LOW);
    Serial.begin(115200);
    Serial.println("System Ready...");

    WiFi.begin("Emosky_group", "emoskynet12@@12");
    while (WiFi.status() != WL_CONNECTED) {
        delay(1000);
        Serial.println("Connecting to WiFi...");
    }
    Serial.println("Connected to WiFi");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
   
    // Register counter with server
    registerCounter();
    
    // Test HTTP connection first
    testHttpConnection();
   
    // Check balance immediately on startup
    checkBalance();
}

void loop() {
    checkButton();
    checkFlowSensor();
   
    // Check web status more frequently
    if (millis() - lastWebCheck > WEB_CHECK_INTERVAL) {
        handleWebValveControl();
        lastWebCheck = millis();
    }
   
    // Periodically check for new payments
    static unsigned long lastPaymentCheck = 0;
    if (millis() - lastPaymentCheck > 300000) { // Every 5 minutes
        checkForNewPayment();
        lastPaymentCheck = millis();
    }
   
    // Periodically report status to server when valve is open
    if ((valveOpen || usageAmount > 0) && millis() - lastStatusUpdate > STATUS_UPDATE_INTERVAL) {
        reportStatus();
        lastStatusUpdate = millis();
    }
}

void registerCounter() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    WiFiClient client;
    HTTPClient http;
    
    String url = "http://" + String(serverAddress) + ":" + String(serverPort) + "/api/register-counter";
    Serial.println("Registering counter with server: " + url);
    
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    StaticJsonDocument<200> doc;
    doc["user_id"] = userId;
    doc["counter_id"] = counterId;
    
    String jsonPayload;
    serializeJson(doc, jsonPayload);
    
    Serial.println("Sending registration data: " + jsonPayload);
    int httpCode = http.POST(jsonPayload);
    
    if (httpCode == HTTP_CODE_OK) {
        Serial.printf("Counter %s registered successfully\n", counterId.c_str());
    } else {
        Serial.printf("Failed to register counter, HTTP code: %d\n", httpCode);
        Serial.println("Error: " + http.errorToString(httpCode));
    }
    http.end();
}

void resetDeviceState() {
    // Close the valve
    digitalWrite(SOLENOID_VALVE, LOW);
    valveOpen = false;
   
    // Reset counters
    pulseCount = 0;
   
    // Don't reset turnCount here as it should be fetched from the server
   
    // Report any accumulated usage
    if (usageAmount > 0) {
        reportWaterUsage();
    }
   
    // Update valve status
    updateValveStatus("close");
   
    // Check balance to get the correct turn count
    checkBalance();
   
    Serial.println("Device state has been reset");
}

void testHttpConnection() {
    WiFiClient client;
    HTTPClient http;
   
    // Try to connect to a known working endpoint
    String testUrl = "http://httpbin.org/get";
    Serial.println("Testing HTTP connection to: " + testUrl);
   
    http.begin(client, testUrl);
    int httpCode = http.GET();
   
    if (httpCode > 0) {
        Serial.printf("HTTP test successful, code: %d\n", httpCode);
        String payload = http.getString();
        Serial.println("Response: " + payload.substring(0, 100) + "...");
    } else {
        Serial.printf("HTTP test failed, error: %s\n", http.errorToString(httpCode).c_str());
    }
   
    http.end();
}

void reportStatus() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected. Cannot report status.");
        return;
    }
   
    // Ensure turn count never goes below zero before reporting
    if (turnCount < 0) {
        turnCount = 0;
    }
   
    WiFiClient client;
    HTTPClient http;
    
    String url = "http://" + String(serverAddress) + ":" + String(serverPort) + "/api/update-status/" + userId;
    
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    
    StaticJsonDocument<200> doc;
    doc["remaining_turns"] = turnCount;
    doc["valve_status"] = valveOpen ? "open" : "closed";
    doc["current_usage"] = usageAmount;
    doc["counter_id"] = counterId;
    
    String jsonPayload;
    serializeJson(doc, jsonPayload);
    
    int httpCode = http.POST(jsonPayload);
    
    if (httpCode > 0) {
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            Serial.println("Status response: " + payload);
        } else {
            Serial.printf("Server error: HTTP code: %d\n", httpCode);
        }
    } else {
        Serial.printf("Connection failed: %s (HTTP code: %d)\n", http.errorToString(httpCode).c_str(), httpCode);
    }
    
    http.end();
}

void checkBalance() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi not connected. Cannot check balance.");
        return;
    }
   
    int maxRetries = 3;
    int retryCount = 0;
    int retryDelay = 1000; // Start with 1 second delay
   
    while (retryCount < maxRetries) {
        WiFiClient client;
        HTTPClient http;
       
        // More explicit URL construction
        String url = "http://";
        url += serverAddress;
        url += ":";
        url += String(serverPort);
        url += "/api/check-balance/";
        url += userId;
        url += "?counter_id=";
        url += counterId;  // Add counter ID as a query parameter
       
        Serial.println("Checking balance at: " + url);
       
        http.begin(client, url);
        http.setTimeout(10000);  // Increase timeout to 10 seconds
       
        Serial.println("Sending GET request...");
        int httpCode = http.GET();
        Serial.println("HTTP response code: " + String(httpCode));
       
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            Serial.println("Response payload: " + payload);
           
            // Parse JSON response
            StaticJsonDocument<200> doc;
            DeserializationError error = deserializeJson(doc, payload);
           
            if (!error) {
                float remainingCubicMeters = doc["remaining_cubic_meters"];
                int availableTurns = doc["remaining_turns"];
               
                turnCount = availableTurns;
               
                Serial.print("Balance updated: ");
                Serial.print(remainingCubicMeters);
                Serial.print(" cubic meters (");
                Serial.print(turnCount);
                Serial.println(" turns)");
               
                // Report status after balance update
                reportStatus();
                http.end();
                return;
            } else {
                Serial.print("Failed to parse balance response: ");
                Serial.println(error.c_str());
                Serial.println("Payload: " + payload);
            }
        } else {
            Serial.printf("Failed to check balance, HTTP code: %d\n", httpCode);
            Serial.println("Error: " + http.errorToString(httpCode));
        }
       
        http.end();
       
        // Retry with exponential backoff
        retryCount++;
        if (retryCount < maxRetries) {
            Serial.printf("Retrying in %d ms (attempt %d of %d)...\n", retryDelay, retryCount + 1, maxRetries);
            delay(retryDelay);
            retryDelay *= 2; // Double the delay for next retry
        }
    }
   
    Serial.println("Failed to check balance after multiple attempts");
}

void checkForNewPayment() {
    if (WiFi.status() != WL_CONNECTED) return;
   
    WiFiClient client;
    HTTPClient http;
   
    String url = "http://" + String(serverAddress) + ":" + String(serverPort) + "/api/check-payment/" + userId;
    Serial.println("Checking for new payment at: " + url);
   
    http.begin(client, url);
   
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println("Payment response: " + payload);
       
        // Parse JSON response
        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, payload);
       
        if (!error) {
            float amountPaid = doc["amount_paid"];
           
            if (amountPaid > 0) {
                Serial.printf("New payment detected: RWF %.2f\n", amountPaid);
                // Update balance after payment
                checkBalance();
            } else {
                Serial.println("No new payment found");
            }
        } else {
            Serial.print("Failed to parse payment response: ");
            Serial.println(error.c_str());
        }
    } else {
        Serial.printf("Failed to check payment, HTTP code: %d\n", httpCode);
    }
    http.end();
}


void handleWebValveControl() {
    if (WiFi.status() != WL_CONNECTED) return;
    
    WiFiClient client;
    HTTPClient http;
    
    String url = "http://" + String(serverAddress) + ":" + String(serverPort) + "/api/get-valve-status/" + userId;
    
    http.begin(client, url);
    http.setTimeout(5000); // 5 second timeout
    
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        
        StaticJsonDocument<200> responseDoc;
        DeserializationError error = deserializeJson(responseDoc, payload);
        
        if (!error) {
            String status = responseDoc["status"];
            bool can_open = responseDoc["can_open"];
            
            // Only change valve state if it's different from current state
            if (status == "open" && !valveOpen && can_open) {
                openValve();
            } else if (status == "close" && valveOpen) {
                closeValve();
            }
        }
    }
    http.end();
}


void checkButton() {
    buttonState = digitalRead(BUTTON_PIN) == LOW;

    if (buttonState && !lastButtonState) {  
        if (turnCount > 0) {
            if (!valveOpen) {
                openValve();
            } else {
                closeValve();
            }
        } else {
            Serial.println("No turns available. Please purchase water to operate the valve.");
            borrowWater();
        }
    }
    lastButtonState = buttonState;
}

void borrowWater() {
    if (WiFi.status() != WL_CONNECTED) return;
   
    WiFiClient client;
    HTTPClient http;
   
    String url = "http://" + String(serverAddress) + ":" + String(serverPort) + "/api/borrow-water/" + userId;
    Serial.println("Requesting water loan at: " + url);
   
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
   
    // Add counter ID to the request
    StaticJsonDocument<200> doc;
    doc["counter_id"] = counterId;
    
    String jsonPayload;
    serializeJson(doc, jsonPayload);
   
    int httpCode = http.POST(jsonPayload);
    if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println("Loan response: " + payload);
       
        // Parse JSON response
        StaticJsonDocument<200> doc;
        DeserializationError error = deserializeJson(doc, payload);
       
        if (!error && doc["success"]) {
            float borrowedAmount = doc["borrowed_amount"];
            int borrowedTime = doc["borrowed_time"];
           
            Serial.printf("Water loan approved: %.2f cubic meters\n", borrowedAmount);
           
            // Convert borrowed amount to turns (assuming 1 cubic meter = 200 turns)
            int borrowedTurns = borrowedAmount * 200;
            turnCount += borrowedTurns;
           
            // Report updated status
            reportStatus();
           
            // Now we can open the valve
            if (!valveOpen) {
                openValve();
            }
        } else {
            Serial.println("Water loan request denied");
            if (!error) {
                String errorMsg = doc["error"] | "Unknown";
                Serial.println("Reason: " + errorMsg);
            }
        }
    } else {
        Serial.printf("Failed to request water loan, HTTP code: %d\n", httpCode);
    }
    http.end();
}

void checkFlowSensor() {
    // Ensure turn count never goes below zero
    if (turnCount < 0) {
        turnCount = 0;
    }
   
    if (valveOpen && turnCount <= 0) {
        Serial.println("No more turns available, closing valve...");
        closeValve();
    }
}

bool updateValveStatus(String status, int maxRetries) {
    int retryCount = 0;
    int retryDelay = 1000; // Start with 1 second delay
   
    while (retryCount < maxRetries) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi not connected. Reconnecting...");
            WiFi.reconnect();
            delay(5000);
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("Failed to reconnect to WiFi");
                return false;
            }
        }
       
        WiFiClient client;
        HTTPClient http;
       
        // Use the correct endpoint
        String url = "http://" + String(serverAddress) + ":" + String(serverPort) + "/api/valve-control/" + userId;
       
        Serial.println("Connecting to: " + url);
       
        http.begin(client, url);
        http.setTimeout(5000); // Shorter timeout
        http.addHeader("Content-Type", "application/json");
       
        StaticJsonDocument<200> doc;
        doc["action"] = status;  // Use "action" instead of "status"
        doc["counter_id"] = counterId;
       
        String jsonPayload;
        serializeJson(doc, jsonPayload);
       
        Serial.println("Sending valve status update: " + jsonPayload);
       
        int httpCode = http.POST(jsonPayload);
       
        if (httpCode > 0) {
            if (httpCode == HTTP_CODE_OK) {
                Serial.printf("Valve status '%s' reported successfully\n", status.c_str());
                http.end();
                return true;
            } else {
                Serial.printf("Server error: HTTP code: %d\n", httpCode);
            }
        } else {
            Serial.printf("Connection failed: %s (HTTP code: %d)\n", http.errorToString(httpCode).c_str(), httpCode);
        }
       
        http.end();
       
        // Exponential backoff
        retryCount++;
        if (retryCount < maxRetries) {
            Serial.printf("Retrying in %d ms (attempt %d of %d)...\n", retryDelay, retryCount + 1, maxRetries);
            delay(retryDelay);
            retryDelay *= 2; // Double the delay for next retry
        }
    }
   
    return false;
}





void openValve() {
    if (turnCount > 0) {
        digitalWrite(SOLENOID_VALVE, HIGH);
        valveOpen = true;
        hasSentUsage = false;  // Reset usage tracking
        Serial.println("Valve opened");
        
        // Send immediate status update
        StaticJsonDocument<200> doc;
        doc["remaining_turns"] = turnCount;
        doc["valve_status"] = "open";
        doc["current_usage"] = usageAmount;
        doc["counter_id"] = counterId;
        
        String jsonPayload;
        serializeJson(doc, jsonPayload);
        
        WiFiClient client;
        HTTPClient http;
        String url = "http://" + String(serverAddress) + ":" + String(serverPort) + "/api/update-status/" + userId;
        http.begin(client, url);
        http.addHeader("Content-Type", "application/json");
        http.POST(jsonPayload);
        http.end();
        
        // Update valve status
        updateValveStatus("open");
    }
}
void closeValve() {
    digitalWrite(SOLENOID_VALVE, LOW);
    valveOpen = false;
    Serial.println("Valve closed.");
   
    // Send immediate status update
    StaticJsonDocument<200> doc;
    doc["remaining_turns"] = turnCount;
    doc["valve_status"] = "closed";
    doc["current_usage"] = usageAmount;
    doc["counter_id"] = counterId;
    
    String jsonPayload;
    serializeJson(doc, jsonPayload);
    
    // Send immediate status update
    WiFiClient client;
    HTTPClient http;
    String url = "http://" + String(serverAddress) + ":" + String(serverPort) + "/api/update-status/" + userId;
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
    http.POST(jsonPayload);
    http.end();
   
    // Report water usage if any
    if (usageAmount > 0 && !hasSentUsage) {
        Serial.printf("Reporting water usage of %.3f cubic meters\n", usageAmount);
        
        // Send usage data to record-usage endpoint
        String usageUrl = "http://" + String(serverAddress) + ":" + String(serverPort) + "/api/record-usage";
        http.begin(client, usageUrl);
        http.addHeader("Content-Type", "application/json");
        
        StaticJsonDocument<200> usageDoc;
        usageDoc["user_id"] = userId;
        usageDoc["counter_id"] = counterId;
        usageDoc["usage_amount"] = usageAmount;
        
        String usagePayload;
        serializeJson(usageDoc, usagePayload);
        
        Serial.println("Sending usage data: " + usagePayload);
        int httpCode = http.POST(usagePayload);
        
        if (httpCode == HTTP_CODE_OK) {
            Serial.println("Water usage recorded successfully");
            usageAmount = 0;  // Reset usage amount after successful recording
            hasSentUsage = true;  // Mark that we've sent the usage
        } else {
            Serial.printf("Failed to record usage, HTTP code: %d\n", httpCode);
            Serial.println("Error: " + http.errorToString(httpCode));
        }
        http.end();
    }
   
    // Update valve status
    updateValveStatus("close");
}

void reportWaterUsage() {
    if (usageAmount <= 0) {
        Serial.println("No usage to report");
        return;
    }
   
    WiFiClient client;
    HTTPClient http;
   
    String url = "http://" + String(serverAddress) + ":" + String(serverPort) + "/api/record-usage";
    Serial.println("Reporting water usage to: " + url);
   
    http.begin(client, url);
    http.addHeader("Content-Type", "application/json");
   
    StaticJsonDocument<200> doc;
    doc["user_id"] = userId;
    doc["counter_id"] = counterId;
    doc["usage_amount"] = usageAmount;
   
    String jsonPayload;
    serializeJson(doc, jsonPayload);
   
    Serial.println("Sending usage data: " + jsonPayload);
    int httpCode = http.POST(jsonPayload);
   
    Serial.printf("HTTP response code: %d\n", httpCode);
   
    if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        Serial.println("Response: " + response);
        Serial.printf("Usage of %.3f cubic meters reported successfully\n", usageAmount);
        usageAmount = 0;  // Reset usage amount after successful report
    } else {
        Serial.printf("Failed to report usage, HTTP code: %d\n", httpCode);
        Serial.println("Error: " + http.errorToString(httpCode));
        // Don't reset hasSentUsage if the report failed
    }
    http.end();
}
