from flask import Flask, render_template, request, redirect, url_for, flash, jsonify
from flask_login import LoginManager, UserMixin, login_user, login_required, logout_user, current_user
from werkzeug.security import generate_password_hash, check_password_hash
import qrcode, io, base64, os, uuid
from datetime import datetime, timedelta
from pyairtable import Api
from apscheduler.schedulers.background import BackgroundScheduler
from email.mime.multipart import MIMEMultipart
from email.mime.text import MIMEText
from email.mime.image import MIMEImage
import smtplib
from dotenv import load_dotenv
from flask_socketio import SocketIO, emit
import threading
import time
import secrets

load_dotenv()

app = Flask(__name__)
app.secret_key = os.environ.get('FLASK_SECRET_KEY', 'your-secret-key')

# Initialize SocketIO
socketio = SocketIO(app, cors_allowed_origins="*")

# Airtable Configuration
AIRTABLE_API_KEY = os.environ.get('AIRTABLE_API_KEY')
AIRTABLE_BASE_ID = os.environ.get('AIRTABLE_BASE_ID')
api = Api(AIRTABLE_API_KEY)

# Initialize Airtable tables
reservations_table = api.table(AIRTABLE_BASE_ID, 'Reservations')
users_table = api.table(AIRTABLE_BASE_ID, 'Users')
environment_table = api.table(AIRTABLE_BASE_ID, 'Environment')

# Flask-Login Setup
login_manager = LoginManager()
login_manager.init_app(app)
login_manager.login_view = 'login'

class User(UserMixin):
    def __init__(self, user_record):
        self.id = user_record['id']
        self.data = user_record['fields']
        self.data.setdefault('Phone', '')
        # Set UID if it doesn't exist
        if 'UID' not in self.data:
            self.data['UID'] = str(uuid.uuid4())
            # Update the record in Airtable
            try:
                users_table.update(self.id, {'UID': self.data['UID']})
            except:
                pass

@login_manager.user_loader
def load_user(user_id):
    try:
        user_rec = users_table.get(user_id)
        return User(user_rec) if user_rec else None
    except Exception as e:
        print(f"Airtable error loading user: {e}")
        return None

# Email Configuration
SMTP_SERVER = 'smtp.gmail.com'
SMTP_PORT = 587
EMAIL_ADDRESS = os.environ.get('EMAIL_ADDRESS')
EMAIL_PASSWORD = os.environ.get('EMAIL_PASSWORD')

# Global variables for sensor data
latest_sensor_data = {
    'temperature': 0,
    'air_quality': 0,
    'timestamp': datetime.now().isoformat()
}

def send_password_reset_email(user_data, reset_token):
    """Send password reset email with forest green theme"""
    try:
        msg = MIMEMultipart()
        msg['From'] = EMAIL_ADDRESS
        msg['To'] = user_data['Email']
        msg['Subject'] = f"🍃 Car Parking Setting 12 - Password Reset Request"

        body = f"""
        <!DOCTYPE html>
        <html lang="en">
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <title>Password Reset - Car Parking Setting 12</title>
            <style>
                body {{
                    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
                    background-color: #f0f8e8;
                    margin: 0;
                    padding: 20px;
                }}
                .container {{
                    max-width: 600px;
                    margin: 0 auto;
                    background: white;
                    border-radius: 15px;
                    overflow: hidden;
                    box-shadow: 0 8px 25px rgba(45, 80, 22, 0.15);
                }}
                .header {{
                    background: linear-gradient(135deg, #2d5016 0%, #4a7c59 100%);
                    color: white;
                    padding: 30px 20px;
                    text-align: center;
                }}
                .content {{
                    padding: 30px 20px;
                }}
                .reset-button {{
                    display: inline-block;
                    background: linear-gradient(135deg, #2d5016, #4a7c59);
                    color: white;
                    text-decoration: none;
                    padding: 15px 30px;
                    border-radius: 25px;
                    font-weight: bold;
                    margin: 20px 0;
                }}
                .footer {{
                    background: #1b2e0a;
                    color: #9acd32;
                    padding: 20px;
                    text-align: center;
                }}
                @media (max-width: 600px) {{
                    .container {{ margin: 10px; }}
                    .header, .content {{ padding: 20px 15px; }}
                }}
            </style>
        </head>
        <body>
            <div class="container">
                <div class="header">
                    <h1>🍃 Password Reset Request</h1>
                    <p>Car Parking Setting 12</p>
                </div>
                <div class="content">
                    <p>Dear <strong>{user_data['Name']}</strong>,</p>
                    <p>We received a request to reset your password for your Car Parking Setting 12 account.</p>
                    <p>Click the button below to reset your password:</p>
                    <div style="text-align: center;">
                        <a href="https://manatee-modern-smoothly.ngrok-free.app/reset-password/{reset_token}" class="reset-button">
                            🔑 Reset My Password
                        </a>
                    </div>
                    <p><strong>This link will expire in 1 hour for security reasons.</strong></p>
                    <p>If you didn't request this password reset, please ignore this email or contact our support team.</p>
                    <div style="background: #e8f5e8; padding: 15px; border-radius: 10px; margin: 20px 0;">
                        <p><strong>🆘 Need Help?</strong></p>
                        <p>📞 Support: +324 89 66 00 93</p>
                        <p>📧 Email: carparkingsetting12@gmail.com</p>
                    </div>
                </div>
                <div class="footer">
                    <p>🍃 Car Parking Setting 12 - Eco-friendly parking solutions</p>
                    <p>&copy; 2025 All rights reserved</p>
                </div>
            </div>
        </body>
        </html>
        """
        
        msg.attach(MIMEText(body, 'html'))

        with smtplib.SMTP(SMTP_SERVER, SMTP_PORT) as server:
            server.starttls()
            server.login(EMAIL_ADDRESS, EMAIL_PASSWORD)
            server.send_message(msg)
        print(f"Password reset email sent to {user_data['Email']}")
        return True
    except Exception as e:
        print(f"Password reset email error: {str(e)}")
        return False

@app.route('/forgot-password', methods=['GET', 'POST'])
def forgot_password():
    if request.method == 'POST':
        email = request.form['email'].strip().lower()
        
        try:
            # Find user by email
            user_record = users_table.first(formula=f"LOWER({{Email}}) = '{email}'")
            
            if user_record:
                # Generate reset token
                reset_token = secrets.token_urlsafe(32)
                expiry_time = (datetime.now() + timedelta(hours=1)).isoformat()
                
                # Update user with reset token
                users_table.update(user_record['id'], {
                    'ResetToken': reset_token,
                    'ResetTokenExpiry': expiry_time
                })
                
                # Send reset email
                if send_password_reset_email(user_record['fields'], reset_token):
                    flash('Password reset email sent! Check your inbox.')
                else:
                    flash('Failed to send reset email. Please try again.')
            else:
                # Don't reveal if email exists or not for security
                flash('If an account with that email exists, a reset link has been sent.')
                
        except Exception as e:
            flash('An error occurred. Please try again.')
            print(f"Forgot password error: {e}")
            
        return redirect(url_for('forgot_password'))
    
    return render_template('forgot_password.html')

@app.route('/reset-password/<token>', methods=['GET', 'POST'])
def reset_password(token):
    try:
        # Find user by reset token
        user_record = users_table.first(formula=f"{{ResetToken}} = '{token}'")
        
        if not user_record:
            flash('Invalid or expired reset link.')
            return redirect(url_for('login'))
        
        # Check if token is expired
        expiry_time = user_record['fields'].get('ResetTokenExpiry')
        if expiry_time:
            expiry_datetime = datetime.fromisoformat(expiry_time)
            if datetime.now() > expiry_datetime:
                flash('Reset link has expired. Please request a new one.')
                return redirect(url_for('forgot_password'))
        
        if request.method == 'POST':
            new_password = request.form['password']
            confirm_password = request.form['confirm_password']
            
            if new_password != confirm_password:
                flash('Passwords do not match.')
                return render_template('reset_password.html', token=token)
            
            if len(new_password) < 6:
                flash('Password must be at least 6 characters long.')
                return render_template('reset_password.html', token=token)
            
            # Update password and clear reset token
            users_table.update(user_record['id'], {
                'Password': generate_password_hash(new_password),
                'ResetToken': None,
                'ResetTokenExpiry': None
            })
            
            flash('Password successfully reset! You can now log in.')
            return redirect(url_for('login'))
        
        return render_template('reset_password.html', token=token, user_name=user_record['fields'].get('Name', ''))
        
    except Exception as e:
        flash('An error occurred. Please try again.')
        print(f"Reset password error: {e}")
        return redirect(url_for('login'))

def get_user_by_uid(uid):
    """Get user data by UID"""
    try:
        user_record = users_table.first(formula=f"{{UID}} = '{uid}'")
        return user_record['fields'] if user_record else None
    except Exception as e:
        print(f"Error fetching user by UID: {e}")
        return None

def get_current_local_time():
    """Get current local time without timezone conversion"""
    return datetime.now()

def get_latest_sensor_data_from_airtable():
    """Fetch the latest sensor data from Airtable Environment table"""
    try:
        records = environment_table.all(
            sort=['-Created'],
            max_records=1
        )
        
        if records:
            fields = records[0]['fields']
            return {
                'temperature': fields.get('Temperature', 0),
                'air_quality': fields.get('AirQuality', 0),
                'timestamp': fields.get('Created', datetime.now().isoformat()),
                'record_id': records[0]['id']
            }
        else:
            return {
                'temperature': 22.0,
                'air_quality': 75,
                'timestamp': datetime.now().isoformat(),
                'record_id': None
            }
    except Exception as e:
        print(f"Error fetching sensor data from Airtable: {e}")
        return {
            'temperature': 22.0,
            'air_quality': 75,
            'timestamp': datetime.now().isoformat(),
            'record_id': None
        }

def get_air_quality_status(value):
    """Get air quality status based on sensor value"""
    if value < 50:
        return {'status': 'Excellent', 'color': 'success'}
    elif value < 100:
        return {'status': 'Good', 'color': 'info'}
    elif value < 150:
        return {'status': 'Moderate', 'color': 'warning'}
    else:
        return {'status': 'Poor', 'color': 'danger'}

def get_temperature_status(value):
    """Get temperature status"""
    if value < 18:
        return {'status': 'Cold', 'color': 'info'}
    elif value < 24:
        return {'status': 'Comfortable', 'color': 'success'}
    elif value < 26:
        return {'status': 'Warm', 'color': 'warning'}
    else:
        return {'status': 'Hot', 'color': 'danger'}

def update_sensor_data():
    """Background thread to get sensor data from Airtable"""
    global latest_sensor_data
    
    while True:
        try:
            airtable_data = get_latest_sensor_data_from_airtable()
            
            temperature = float(airtable_data.get('temperature', 22.0))
            air_quality = int(airtable_data.get('air_quality', 75))
            
            latest_sensor_data = {
                'temperature': temperature,
                'air_quality': air_quality,
                'timestamp': datetime.now().isoformat(),
                'air_quality_status': get_air_quality_status(air_quality),
                'temperature_status': get_temperature_status(temperature),
                'airtable_timestamp': airtable_data.get('timestamp')
            }
            
            socketio.emit('sensor_update', latest_sensor_data)
            time.sleep(5)
            
        except Exception as e:
            print(f"Error updating sensor data: {e}")
            time.sleep(10)

def is_valid_reservation_time(date_str, start_time_str):
    """Check if the reservation time is valid (not in the past and on 5-minute intervals)"""
    try:
        current_time = get_current_local_time()
        
        # Parse the reservation datetime
        reservation_datetime = datetime.strptime(f"{date_str} {start_time_str}", '%Y-%m-%d %H:%M')
        
        # Check if time is on 5-minute intervals
        if reservation_datetime.minute % 5 != 0:
            return False, "Reservation time must be on 5-minute intervals (e.g., 21:20, 21:25, 21:30)"
        
        # 5-minute buffer for same-day reservations
        minimum_allowed_time = current_time + timedelta(minutes=5)
        
        if reservation_datetime < minimum_allowed_time:
            return False, f"Reservation time must be at least 5 minutes from now. Current time: {current_time.strftime('%H:%M')}"
        
        return True, "Valid time"
        
    except Exception as e:
        return False, f"Invalid date/time format: {str(e)}"
 
def get_min_datetime():
    """Get the minimum datetime for datetime-local input"""
    current_time = get_current_local_time()
    # Round up to next 5-minute interval and add 5 minutes buffer
    minutes = current_time.minute
    next_5_min = ((minutes // 5) + 2) * 5  # +2 for 5-minute buffer and next slot
    
    if next_5_min >= 60:
        next_hour = current_time.hour + 1
        next_5_min = 0
        if next_hour >= 24:
            # Move to next day
            next_day = current_time + timedelta(days=1)
            return next_day.strftime('%Y-%m-%dT00:00')
    else:
        next_hour = current_time.hour
    
    return f"{current_time.strftime('%Y-%m-%d')}T{next_hour:02d}:{next_5_min:02d}"

def send_exit_reminder_email(reservation_record):
    """Send email reminder 15 minutes before reservation ends"""
    try:
        # Get user data by UID
        user_uid = reservation_record['fields'].get('UID', '')
        user_data = get_user_by_uid(user_uid)
        
        if not user_data:
            print(f"User not found for UID: {user_uid}")
            return False
        
        msg = MIMEMultipart()
        msg['From'] = EMAIL_ADDRESS
        msg['To'] = user_data['Email']
        msg['Subject'] = f"🍃 Car Parking Setting 12 - Reservation Ending Soon!"

        raw_date = reservation_record['fields']['Date']
        try:
            date_obj = datetime.strptime(raw_date, '%Y-%m-%d')
            formatted_date = date_obj.strftime('%A, %B %d, %Y')
        except Exception:
            formatted_date = raw_date

        start_time = reservation_record['fields'].get('StartTime', '')
        end_time = reservation_record['fields'].get('EndTime', '')

        body = f"""
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
        </head>
        <body style="font-family: Arial, sans-serif; background-color: #f0f8e8; margin: 0; padding: 20px;">
            <div style="max-width: 600px; margin: 0 auto; background: white; border-radius: 15px; padding: 30px;">
                <h1 style="color: #ff6b35; text-align: center;">⏰ Reservation Ending Soon!</h1>
                <h2 style="color: #2d5016; text-align: center;">Car Parking Setting 12</h2>
                
                <div style="background: #fff3cd; padding: 20px; border-radius: 15px; margin: 20px 0; text-align: center;">
                    <h2 style="color: #ff4757;">🚨 URGENT NOTICE</h2>
                    <p style="font-size: 18px;">Your garage reservation is ending soon!</p>
                </div>
                
                <p>Dear <strong>{user_data['Name']}</strong>,</p>
                <p>This is an important reminder that your garage reservation will end soon. Please prepare to exit the garage.</p>
                
                <div style="background: #fff3cd; padding: 20px; border-radius: 15px; margin: 20px 0;">
                    <h3 style="color: #2d5016;">📋 Your Reservation Details</h3>
                    <p><strong>📅 Date:</strong> {formatted_date}</p>
                    <p><strong>🕐 Time:</strong> {start_time} - {end_time}</p>
                </div>
                
                <div style="background: #d1ecf1; padding: 20px; border-radius: 15px; margin: 20px 0; text-align: center;">
                    <h3 style="color: #0c5460;">🚨 Important: You Must Exit Soon!</h3>
                    <div style="font-size: 24px; font-weight: bold; color: #ff4757; margin: 15px 0; padding: 15px; background: white; border-radius: 10px;">
                        Reservation ends at {end_time}
                    </div>
                    <p><strong>Grace period:</strong> You have until <strong>{end_time} + 5 minutes</strong> to exit</p>
                </div>
                
                <div style="background: #d4edda; padding: 20px; border-radius: 15px; margin: 20px 0;">
                    <h3 style="color: #155724;">📱 How to Exit</h3>
                    <ol style="color: #155724;">
                        <li><strong>Drive to the exit gate</strong></li>
                        <li><strong>Scan the same QR code</strong> you used for entry</li>
                        <li><strong>Exit gate will open</strong> if you're within the allowed time</li>
                    </ol>
                </div>
                
                <div style="background: #f8d7da; padding: 20px; border-radius: 15px; margin: 20px 0; text-align: center;">
                    <h3 style="color: #721c24;">🆘 Need Immediate Help?</h3>
                    <p>If you're having trouble exiting or need assistance:</p>
                    <a href="tel:+32489660093" style="display: inline-block; background: #dc3545; color: white; text-decoration: none; padding: 15px 30px; border-radius: 25px; font-weight: bold; margin: 10px;">📞 +324 89 66 00 93</a>
                    <p style="font-weight: bold;">Call now for immediate support!</p>
                </div>
                
                <div style="text-align: center; color: #721c24; font-weight: bold; margin: 25px 0; padding: 15px; background: rgba(248, 215, 218, 0.5); border-radius: 10px;">
                    ⚠️ If you don't exit by {end_time} + 5 minutes, you will need to contact support immediately!
                </div>
                
                <hr style="margin: 30px 0;">
                <div style="text-align: center; color: #6b8e23;">
                    <p><strong>🍃 Car Parking Setting 12</strong></p>
                    <p>Eco-friendly parking solutions with nature in mind</p>
                    <p>&copy; 2025 All rights reserved</p>
                </div>
            </div>
        </body>
        </html>
        """
        
        msg.attach(MIMEText(body, 'html'))

        with smtplib.SMTP(SMTP_SERVER, SMTP_PORT) as server:
            server.starttls()
            server.login(EMAIL_ADDRESS, EMAIL_PASSWORD)
            server.send_message(msg)
        print(f"Exit reminder email sent to {user_data['Email']}")
        return True
    except Exception as e:
        print(f"Exit reminder email error: {str(e)}")
        return False

def cleanup_old_reservations():
    """Delete expired, cancelled, and completed reservations after 5 minutes"""
    try:
        current_time = datetime.now()
        
        print(f"Running cleanup at {current_time.strftime('%Y-%m-%d %H:%M:%S')}")
        
        # Find reservations to delete
        reservations_to_delete = reservations_table.all(
            formula="OR(Statuss = 'Expired', Statuss = 'Cancelled', Statuss = 'Scanned Twice')"
        )
        
        deleted_count = 0
        
        for reservation in reservations_to_delete:
            try:
                fields = reservation['fields']
                
                # Get the creation time
                created_at_str = fields.get('CreatedAt', '')
                
                # If no CreatedAt field, delete immediately (old records)
                if not created_at_str:
                    should_delete = True
                else:
                    try:
                        created_at = datetime.strptime(created_at_str, '%Y-%m-%d %H:%M:%S')
                        # Check if 5 minutes have passed since creation for completed/expired/cancelled
                        should_delete = (current_time - created_at) > timedelta(minutes=5)
                    except:
                        # If parsing fails, delete it
                        should_delete = True
                
                if should_delete:
                    # Delete the reservation
                    reservations_table.delete(reservation['id'])
                    deleted_count += 1
                    print(f"🗑️ Deleted reservation {reservation['id']} (Status: {fields.get('Statuss', 'Unknown')})")
                    
            except Exception as e:
                print(f"Error deleting reservation {reservation['id']}: {e}")
        
        if deleted_count > 0:
            print(f"✅ Cleanup complete: {deleted_count} old reservations deleted")
        else:
            print("✅ Cleanup complete: No old reservations to delete")
            
    except Exception as e:
        print(f"❌ Cleanup error: {e}")

def check_reservations():
    """Enhanced reservation checker with exit reminders and grace periods"""
    now = get_current_local_time()
    
    try:
        # Find active reservations and those with entry recorded
        active_reservations = reservations_table.all(
            formula="OR(Statuss = 'Active', Statuss = 'Scanned Once')"
        )
        
        for record in active_reservations:
            fields = record['fields']
            reservation_date = fields.get('Date', '')
            end_time = fields.get('EndTime', '')
            status = fields.get('Statuss', '')
            
            if reservation_date and end_time:
                # Parse end time
                try:
                    end_datetime = datetime.strptime(f"{reservation_date} {end_time}", '%Y-%m-%d %H:%M')
                    
                    # Calculate reminder time (15 minutes before end)
                    reminder_time = end_datetime - timedelta(minutes=15)
                    
                    # Calculate grace period end (5 minutes after end)
                    grace_end_time = end_datetime + timedelta(minutes=5)
                    
                    # Check if it's time to send reminder email
                    if (status == 'Scanned Once' and 
                        now >= reminder_time and 
                        now < end_datetime and
                        not fields.get('ReminderSent')):
                        
                        print(f"Sending exit reminder for reservation {record['id']}")
                        if send_exit_reminder_email(record):
                            # Mark reminder as sent
                            reservations_table.update(record['id'], {'ReminderSent': True})
                    
                    # Check if grace period has ended
                    elif now > grace_end_time and not fields.get('ExitTime'):
                        print(f"Scheduler: Marking reservation {record['id']} as 'Expired' - Grace period ended")
                        reservations_table.update(record['id'], {
                            'Statuss': 'Expired', 
                            'Notes': f'Auto-expired: Grace period ended at {now.strftime("%Y-%m-%d %H:%M")}'
                        })
                        
                except Exception as e:
                    print(f"Error processing reservation {record['id']}: {e}")
            
    except Exception as e:
        print(f"Scheduler error: {e}")

# SENSOR ROUTES
@app.route('/sensors')
@login_required
def sensors():
    """Live sensor monitoring page"""
    return render_template('sensors.html')

@app.route('/api/sensor-data')
@login_required
def get_sensor_data():
    """API endpoint to get current sensor data"""
    return jsonify(latest_sensor_data)

@app.route('/api/sensor-history')
@login_required
def get_sensor_history():
    """API endpoint to get historical sensor data"""
    try:
        records = environment_table.all(
            sort=['-Created'],
            max_records=50
        )
        
        history = []
        for record in records:
            fields = record['fields']
            history.append({
                'temperature': fields.get('Temperature', 0),
                'air_quality': fields.get('AirQuality', 0),
                'timestamp': fields.get('Created', ''),
                'formatted_time': datetime.fromisoformat(fields.get('Created', '')).strftime('%H:%M:%S') if fields.get('Created') else ''
            })
        
        history.reverse()
        return jsonify(history)
        
    except Exception as e:
        print(f"Error fetching sensor history: {e}")
        return jsonify([])

# SocketIO event handlers
@socketio.on('connect')
def handle_connect():
    """Handle client connection"""
    print('Client connected to sensor monitoring')
    emit('sensor_update', latest_sensor_data)

@socketio.on('disconnect')
def handle_disconnect():
    """Handle client disconnection"""
    print('Client disconnected from sensor monitoring')

@app.route('/cancel_reservation/<reservation_id>', methods=['GET', 'POST'])
@login_required
def cancel_reservation(reservation_id):
    try:
        reservation = reservations_table.get(reservation_id)
        
        if not reservation or reservation['fields'].get('UID', '') != current_user.data.get('UID', ''):
            flash('Reservation not found or you do not have permission to cancel it.')
            return redirect(url_for('index'))
        
        status = reservation['fields'].get('Statuss', '')
        if status in ['Expired', 'Scanned Twice']:
            flash('This reservation cannot be cancelled as it is already completed or expired.')
            return redirect(url_for('index'))
            
        if request.method == 'POST':
            reservations_table.update(reservation_id, {
                'Statuss': 'Cancelled',
                'QRCodeImageB64': None
            })
            flash('Reservation successfully cancelled.')
            return redirect(url_for('index'))
        
        # Get user data for display
        user_data = get_user_by_uid(reservation['fields'].get('UID', ''))
        reservation['user_data'] = user_data
        
        return render_template('cancel_confirmation.html', reservation=reservation)
        
    except Exception as e:
        flash(f'Error cancelling reservation: {str(e)}')
        print(f"Cancellation error: {e}")
        return redirect(url_for('index'))

def get_user_active_reservations_count(user_uid):
    try:
        formula = f"AND({{UID}} = '{user_uid}', OR({{Statuss}} = 'Active', {{Statuss}} = 'Scanned Once'))"
        user_reservations = reservations_table.all(formula=formula)
        
        reservation_details = []
        for res in user_reservations:
            fields = res['fields']
            reservation_details.append({
                'date': fields.get('Date', 'N/A'),
                'start_time': fields.get('StartTime', 'N/A'),
                'end_time': fields.get('EndTime', 'N/A'),
                'status': fields.get('Statuss', 'N/A')
            })
            
        return len(user_reservations), reservation_details
    except Exception as e:
        print(f"Error checking user reservations: {str(e)}")
        return 0, []

def generate_qr_for_reservation(reservation_id):
    try:
        qr_data = reservation_id 
        qr = qrcode.QRCode(version=1, box_size=10, border=4)
        qr.add_data(qr_data)
        qr.make(fit=True)
        img = qr.make_image(fill_color="black", back_color="white")
        
        img_byte_arr = io.BytesIO()
        img.save(img_byte_arr, format='PNG')
        img_byte_arr.seek(0)
        return base64.b64encode(img_byte_arr.getvalue()).decode('utf-8')
        
    except Exception as e:
        print(f"Error generating QR image for reservation ID {reservation_id}: {str(e)}")
        raise

def send_confirmation_email(reservation_record):
    try:
        # Get user data by UID
        user_uid = reservation_record['fields'].get('UID', '')
        user_data = get_user_by_uid(user_uid)
        
        if not user_data:
            print(f"User not found for UID: {user_uid}")
            return False
        
        msg = MIMEMultipart()
        msg['From'] = EMAIL_ADDRESS
        msg['To'] = user_data['Email']
        msg['Subject'] = f"🍃 Car Parking Setting 12 - Reservation Confirmed"

        raw_date = reservation_record['fields']['Date']
        try:
            date_obj = datetime.strptime(raw_date, '%Y-%m-%d')
            formatted_date = date_obj.strftime('%A, %B %d, %Y')
        except Exception:
            formatted_date = raw_date

        start_time = reservation_record['fields'].get('StartTime', '')
        end_time = reservation_record['fields'].get('EndTime', '')

        body = f"""
        <!DOCTYPE html>
        <html>
        <head>
            <meta charset="UTF-8">
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
        </head>
        <body style="font-family: Arial, sans-serif; background-color: #f0f8e8; margin: 0; padding: 20px;">
            <div style="max-width: 600px; margin: 0 auto; background: white; border-radius: 15px; padding: 30px; box-shadow: 0 4px 15px rgba(45, 80, 22, 0.15);">
                <div style="text-align: center; margin-bottom: 30px;">
                    <h1 style="color: #2d5016; margin: 0;">🍃 Car Parking Setting 12</h1>
                    <h2 style="color: #4a7c59; margin: 10px 0;">Reservation Confirmed!</h2>
                </div>
                
                <p>Dear <strong>{user_data['Name']}</strong>,</p>
                <p>Great news! Your garage reservation has been successfully confirmed.</p>
                
                <div style="background: linear-gradient(135deg, #e8f5e8, #f0f8e8); border: 2px solid #6b8e23; border-radius: 15px; padding: 25px; margin: 25px 0;">
                    <h3 style="color: #2d5016; margin-top: 0;">📋 Reservation Details</h3>
                    <p><strong>📅 Date:</strong> {formatted_date}</p>
                    <p><strong>🕐 Time:</strong> {start_time} - {end_time}</p>
                    <p><strong>👤 Name:</strong> {user_data['Name']}</p>
                    <p><strong>📧 Email:</strong> {user_data['Email']}</p>
                    <p><strong>📱 Phone:</strong> {user_data.get('Phone', 'N/A')}</p>
                </div>
        """

        # Add QR code if available
        if 'QRCodeImageB64' in reservation_record['fields'] and reservation_record['fields']['QRCodeImageB64']:
            body += f"""
                <div style="text-align: center; background: #f0f8e8; padding: 25px; border-radius: 15px; margin: 25px 0; border: 3px solid #2d5016;">
                    <h3 style="color: #2d5016;">📱 Your Access QR Code</h3>
                    <p style="color: #4a7c59;">Scan this code at the garage entry and exit gates</p>
                    <img src="cid:qrcode_image" alt="QR Code" style="max-width: 200px; border: 3px solid #2d5016; border-radius: 10px; padding: 10px; background: white;">
                </div>
            """
            try:
                qr_img_data = base64.b64decode(reservation_record['fields']['QRCodeImageB64'])
                image = MIMEImage(qr_img_data, _subtype="png")
                image.add_header('Content-ID', '<qrcode_image>')
                msg.attach(image)
                print(f"📎 QR code attached successfully")
            except Exception as e:
                print(f"⚠️ Failed to attach QR code: {e}")

        body += f"""
                <div style="background: #fff3cd; border: 2px solid #9acd32; border-radius: 15px; padding: 20px; margin: 25px 0;">
                    <h3 style="color: #2d5016; margin-top: 0;">🚗 How to Use Your Reservation</h3>
                    <ol style="color: #4a7c59;">
                        <li><strong>Save this QR code</strong> to your phone or print this email</li>
                        <li><strong>Arrive anytime</strong> from 15 minutes before your start time until your end time</li>
                        <li><strong>Entry:</strong> Scan the QR code at the entry gate</li>
                        <li><strong>Exit:</strong> Scan the same QR code at the exit gate</li>
                        <li><strong>Grace period:</strong> You have 5 minutes after your end time to exit</li>
                    </ol>
                </div>
                
                <div style="background: #e8f5e8; padding: 20px; border-radius: 10px; margin: 25px 0; text-align: center;">
                    <h3 style="color: #2d5016; margin-top: 0;">🆘 Need Help?</h3>
                    <p style="margin: 10px 0;"><strong>📞 Support:</strong> <a href="tel:+32489660093" style="color: #2d5016; text-decoration: none; font-weight: bold;">+324 89 66 00 93</a></p>
                    <p style="margin: 10px 0;"><strong>📧 Email:</strong> <a href="mailto:carparkingsetting12@gmail.com" style="color: #2d5016; text-decoration: none; font-weight: bold;">carparkingsetting12@gmail.com</a></p>
                </div>
                
                <div style="text-align: center; margin-top: 30px;">
                    <p style="color: #228b22; font-size: 18px; font-weight: bold;">🙏 Thank you for choosing Car Parking Setting 12!</p>
                </div>
                
                <hr style="border: 1px solid #e8f5e8; margin: 30px 0;">
                <div style="text-align: center; color: #6b8e23;">
                    <p><strong>🍃 Car Parking Setting 12</strong></p>
                    <p>Eco-friendly parking solutions with nature in mind</p>
                    <p>&copy; 2025 All rights reserved</p>
                </div>
            </div>
        </body>
        </html>
        """
        
        msg.attach(MIMEText(body, 'html'))

        with smtplib.SMTP(SMTP_SERVER, SMTP_PORT) as server:
            server.starttls()
            server.login(EMAIL_ADDRESS, EMAIL_PASSWORD)
            server.send_message(msg)
        print(f"Confirmation email sent to {user_data['Email']}")
        return True
    except Exception as e:
        print(f"Email sending error: {str(e)}")
        return False

@app.route('/reservation/<reservation_id>')
@login_required
def view_reservation(reservation_id):
    """View a specific reservation with QR code"""
    try:
        reservation = reservations_table.get(reservation_id)
        
        if not reservation:
            flash('Reservation not found.')
            return redirect(url_for('index'))
        
        # Check if user owns this reservation using UID
        if reservation['fields'].get('UID', '') != current_user.data.get('UID', ''):
            flash('You do not have permission to view this reservation.')
            return redirect(url_for('index'))
        
        # Get user data for display
        user_data = get_user_by_uid(reservation['fields'].get('UID', ''))
        reservation['user_data'] = user_data
        
        return render_template('view_reservation.html', reservation=reservation)
        
    except Exception as e:
        print(f"❌ View reservation error: {str(e)}")
        flash(f'Error loading reservation: {str(e)}')
        return redirect(url_for('index'))

@app.route('/')
@login_required
def index():
    try:
        user_uid = current_user.data.get('UID', '')
        user_reservations = reservations_table.all(
            formula=f"AND({{UID}} = '{user_uid}', OR({{Statuss}} = 'Active', {{Statuss}} = 'Scanned Once'))",
            sort=['Date', 'StartTime']
        )
        
        # Add user data to each reservation for display
        for reservation in user_reservations:
            user_data = get_user_by_uid(reservation['fields'].get('UID', ''))
            reservation['user_data'] = user_data
        
        return render_template('index.html', reservations=user_reservations)
    except Exception as e:
        flash(f"Error loading reservations: {str(e)}")
        return render_template('index.html', reservations=[])

@app.route('/register', methods=['GET', 'POST'])
def register():
    if request.method == 'POST':
        email = request.form['email'].strip().lower()
        name = request.form['name'].strip()
        phone = request.form['phone'].strip()
        password = request.form['password']
        password2 = request.form['password2']

        if not all([email, name, phone, password, password2]):
            flash('Please fill out all fields.')
            return redirect(url_for('register'))
        if password != password2:
            flash('Passwords do not match.')
            return redirect(url_for('register'))

        existing = users_table.first(formula=f"LOWER({{Email}}) = '{email}'")
        if existing:
            flash('Email already registered. Please log in.')
            return redirect(url_for('login'))

        try:
            # Generate unique UID for new user
            user_uid = str(uuid.uuid4())
            
            users_table.create({
                'UID': user_uid,
                'Email': email,
                'Name': name,
                'Phone': phone,
                'Password': generate_password_hash(password)
            })
            flash('Registration successful! Please log in.')
            return redirect(url_for('login'))
        except Exception as e:
            flash('Registration failed. Please try again.')
            print(f"Registration error: {e}")
    return render_template('register.html')

@app.route('/login', methods=['GET', 'POST'])
def login():
    if current_user.is_authenticated:
        return redirect(url_for('index'))
    if request.method == 'POST':
        try:
            email_form = request.form['email'].strip().lower()
            user_rec = users_table.first(formula=f"LOWER({{Email}}) = '{email_form}'")
            if user_rec and check_password_hash(user_rec['fields'].get('Password',''), request.form['password']):
                login_user(User(user_rec))
                return redirect(request.args.get('next') or url_for('index'))
            flash('Invalid credentials. Please try again.')
        except Exception as e:
            flash(f'Login error: {str(e)}. Please try again.')
    return render_template('login.html')

@app.route('/reserve', methods=['GET', 'POST'])
@login_required
def reserve():
    if request.method == 'POST':
        try:
            # Parse datetime-local input
            datetime_str = request.form['datetime']
            reservation_datetime = datetime.strptime(datetime_str, '%Y-%m-%dT%H:%M')
            date_str = reservation_datetime.strftime('%Y-%m-%d')
            start_time_str = reservation_datetime.strftime('%H:%M')
            duration = int(request.form['duration'])
            
            # Validate that the reservation time is valid
            is_valid_time, time_message = is_valid_reservation_time(date_str, start_time_str)
            if not is_valid_time:
                flash(f'Invalid reservation time: {time_message}')
                return render_template('reserve.html',
                    datetime=datetime_str,
                    duration=duration,
                    min_datetime=get_min_datetime())
            
            user_uid = current_user.data.get('UID', '')
            reservation_count, existing_reservations = get_user_active_reservations_count(user_uid)
            
            if reservation_count >= 3:
                active_reservations = reservations_table.all(
                    formula=f"AND({{UID}} = '{user_uid}', OR({{Statuss}} = 'Active', {{Statuss}} = 'Scanned Once'))"
                )
                
                flash(f'You already have {reservation_count} active reservations (maximum is 3).')
                return render_template('manage_reservations.html', 
                                      reservations=active_reservations,
                                      new_reservation={
                                          'datetime': datetime_str,
                                          'duration': duration
                                      })
            
            # Calculate end time
            start_datetime = datetime.strptime(f"{date_str} {start_time_str}", '%Y-%m-%d %H:%M')
            end_datetime = start_datetime + timedelta(hours=duration)
            end_time_str = end_datetime.strftime('%H:%M')
            
            # Store the current local time for creation timestamp
            current_time = get_current_local_time()
            
            # Create reservation with UID instead of user data
            reservation_data = {
                'UID': user_uid,  # Store UID instead of Name, Email, Phone
                'Date': date_str,
                'StartTime': start_time_str,
                'EndTime': end_time_str,
                'Statuss': 'Active',
                'CreatedAt': current_time.strftime('%Y-%m-%d %H:%M:%S')
            }
            
            new_reservation_record = reservations_table.create(reservation_data)
            reservation_id = new_reservation_record['id']
            
            qr_image_b64 = generate_qr_for_reservation(reservation_id)
            reservations_table.update(reservation_id, {'QRCodeImageB64': qr_image_b64})
            
            full_reservation_with_qr = reservations_table.get(reservation_id)
            
            if send_confirmation_email(full_reservation_with_qr):
                flash('Reservation created! Check your email for the QR code.')
            else:
                flash('Reservation created but email failed to send.')
            
            return redirect(url_for('index'))
            
        except Exception as e:
            print(f"Reservation creation error: {e}")
            flash(f'Reservation failed: {str(e)}')
            return render_template('reserve.html',
                min_datetime=get_min_datetime())

    return render_template('reserve.html',
        min_datetime=get_min_datetime())

@app.route('/account', methods=['GET', 'POST'])
@login_required
def account():
    if request.method == 'POST':
        try:
            # Get form data
            name = request.form['name'].strip()
            email = request.form['email'].strip().lower()
            phone = request.form['phone'].strip()
            current_password = request.form.get('current_password', '')
            new_password = request.form.get('new_password', '')
            confirm_password = request.form.get('confirm_password', '')
            
            # Validation
            if not all([name, email, phone]):
                flash('Please fill out all required fields.')
                return render_template('account.html', user=current_user.data)
            
            # Check if email is already taken by another user
            existing_user = users_table.first(
                formula=f"AND(LOWER({{Email}}) = '{email}', NOT({{UID}} = '{current_user.data.get('UID', '')}'))"
            )
            if existing_user:
                flash('This email is already registered by another user.')
                return render_template('account.html', user=current_user.data)
            
            # Prepare update data
            update_data = {
                'Name': name,
                'Email': email,
                'Phone': phone
            }
            
            # Handle password change if requested
            if new_password:
                if not current_password:
                    flash('Current password is required to change password.')
                    return render_template('account.html', user=current_user.data)
                
                # Verify current password
                if not check_password_hash(current_user.data.get('Password', ''), current_password):
                    flash('Current password is incorrect.')
                    return render_template('account.html', user=current_user.data)
                
                if new_password != confirm_password:
                    flash('New passwords do not match.')
                    return render_template('account.html', user=current_user.data)
                
                if len(new_password) < 6:
                    flash('New password must be at least 6 characters long.')
                    return render_template('account.html', user=current_user.data)
                
                # Add hashed password to update data
                update_data['Password'] = generate_password_hash(new_password)
            
            # Update user in Airtable
            users_table.update(current_user.id, update_data)
            
            # Update current_user data
            current_user.data.update(update_data)
            
            flash('Account updated successfully!')
            return redirect(url_for('index'))  # Redirect to homepage
            
        except Exception as e:
            flash(f'Error updating account: {str(e)}')
            print(f"Account update error: {e}")
            return render_template('account.html', user=current_user.data)
    
    # GET request - show current user data
    return render_template('account.html', user=current_user.data)

@app.route('/logout')
@login_required
def logout():
    logout_user()
    flash('You have been logged out.')
    return redirect(url_for('login'))

if __name__ == '__main__':
    current_time = datetime.now()
    print(f"Server starting at local time: {current_time.strftime('%Y-%m-%d %H:%M:%S')}")
    
    scheduler = BackgroundScheduler(daemon=True)
    scheduler.add_job(check_reservations, 'interval', minutes=5)
    scheduler.add_job(cleanup_old_reservations, 'interval', minutes=2)
    scheduler.start()
    print("Background scheduler started")
    
    sensor_thread = threading.Thread(target=update_sensor_data, daemon=True)
    sensor_thread.start()
    print("Sensor monitoring thread started (reading from Airtable)")
    
    print("Starting Flask server with SocketIO...")
    print("Sensor data source: Airtable Environment table")
    
    socketio.run(app, host='0.0.0.0', port=5000, debug=False)
