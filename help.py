from flask import Flask, request, jsonify, render_template
from datetime import datetime
import smtplib
from email.mime.text import MIMEText
from email.mime.multipart import MIMEMultipart

app = Flask(__name__)

water_usage = {}
payments = {}

SMTP_SERVER = "smtp.gmail.com"
SMTP_PORT = 587
SMTP_USERNAME = "nathanaelniyogushimwa@gmail.com"
SMTP_PASSWORD = "gilo mrrg fxop embj"

# Admin Routes
@app.route('/admin')
def admin_dashboard():
    return render_template('adminDashboard.html')

@app.route('/users')
def users():
    return render_template('users.html')

@app.route('/water-usage')
def water_usage_page():
    return render_template('water-usage.html')

@app.route('/billing')
def billing():
    return render_template('billing.html')

@app.route('/settings')
def admin_settings():
    return render_template('settings.html')

# User Routes
@app.route('/')
def index():
    return render_template('index.html')

@app.route('/user/dashboard')
def user_dashboard():
    return render_template('User_dashboard.html')

@app.route('/user/consumption')
def consumption():
    return render_template('consumption.html')

@app.route('/user/bills')
def bills():
    return render_template('bills.html')

@app.route('/user/settings')
def user_settings():
    return render_template('User-settings.html')

@app.route('/user/support')
def support():
    return render_template('support.html')

@app.route('/user/report')
def user_report():
    return render_template('user-report.html')

# Payment Routes
@app.route('/user/payment', methods=['GET', 'POST'])
def payment():
    if request.method == 'POST':
        data = request.form
        user_id = data.get('user_id')
        amount = float(data.get('amount'))
        
        if user_id and amount:
            if user_id not in payments:
                payments[user_id] = []
            payments[user_id].append({
                'amount': amount,
                'timestamp': datetime.now(),
                'status': 'success'
            })
            
            return jsonify({
                "message": "Payment successful",
                "water_time": amount * 60
            }), 200
            
    return render_template('payment.html')

# API Endpoints
@app.route('/api/receive-data', methods=['POST'])
def receive_data():
    data = request.json
    user_id = data.get('user_id')
    usage = data.get('usage')

    if user_id and usage:
        water_usage[user_id] = usage
        print(f"User {user_id} used {usage}L of water")
        return jsonify({"message": "Data received successfully"}), 200
    return jsonify({"error": "Invalid data"}), 400

@app.route('/api/water-usage', methods=['GET'])
def get_data():
    return jsonify(water_usage)

@app.route('/api/check-payment/<user_id>', methods=['GET'])
def check_payment(user_id):
    if user_id in payments and payments[user_id]:
        latest_payment = payments[user_id].pop(0)  # Remove the used payment
        water_time = latest_payment['amount'] * 60  # Convert amount to water time
        
        # Store the last granted time and reset it for next check
        if user_id in water_usage:
            water_usage[user_id] = water_time  # Replace previous value
        else:
            water_usage[user_id] = water_time

        return jsonify({
            "amount": latest_payment['amount'],
            "water_time": water_time  # Send the new water time
        })
    
    return jsonify({"water_time": 0})  # If no new payment, return 0


@app.route('/api/send-notification', methods=['POST'])
def send_email():
    data = request.json
    subject = data.get('event')  # Use 'event' instead of 'subject'
    body = data.get('details')   # Use 'details' instead of 'body'

    if subject and body:
        msg = MIMEMultipart()
        msg['From'] = SMTP_USERNAME
        msg['To'] = SMTP_USERNAME  # Sending to the same email
        msg['Subject'] = subject
        msg.attach(MIMEText(body, 'plain'))

        try:
            with smtplib.SMTP(SMTP_SERVER, SMTP_PORT) as server:
                server.starttls()
                server.login(SMTP_USERNAME, SMTP_PASSWORD)
                server.send_message(msg)
            return jsonify({"message": "Email sent successfully"}), 200
        except Exception as e:
            return jsonify({"error": str(e)}), 500
            
    return jsonify({"error": "Missing event or details"}), 400

@app.route('/api/control-valve', methods=['POST'])
def control_valve():
    data = request.json
    user_id = data.get('user_id')
    action = data.get('action')  # 'open' or 'close'

    if user_id:
        water_usage[user_id] = action  # Store the command for NodeMCU to read
        return jsonify({"message": f"Valve {action} command sent"}), 200

    return jsonify({"error": "Invalid request"}), 400

@app.route('/api/get-valve-status/<user_id>', methods=['GET'])
def get_valve_status(user_id):
    action = water_usage.get(user_id, "close")  # Default to closed
    return jsonify({"status": action})


# logout the user
@app.route('/admin/logout')
@login_required
def logout():
    logout_user()
    return redirect(url_for('admin_login'))


if __name__ == '__main__':
    app.run(debug=True, host="0.0.0.0", port=5000)
