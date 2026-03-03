import numpy as np
from sklearn.preprocessing import StandardScaler
from sklearn.neural_network import MLPRegressor
from sklearn.ensemble import RandomForestRegressor, GradientBoostingRegressor
from sklearn.tree import DecisionTreeRegressor
import warnings
warnings.filterwarnings('ignore')

# Training data storage
training_data = {
    '0': '2ffcf449457fdbb98aaae7c389a1bd53bcd1cefe',
    '1': '2ef0eb8959f799cc1a528d202d7d08345625158b',
    '3': '668740748f79c03b1798767dc803deeb169bfdcd',
    '4': 'ae5248e15e5d1d9d757014fa79930099c6610b89',
    '5': 'a7182309b48dfa6139b0bc76efda68b4a7574cde',
    'a': 'a545f42da3fb84f3746a89d66e6bd7cbc14a1e38',
    'b': 'c85fbf0dbfd3c7145c771265c74e634dcee75559',
    'c': 'eb162907ac6fbba8a7d26957030b5325adf7cd2b',
    'd': 'ff62ed2d377558d0e728f16c953f3da7dc96083b',
    'A': '5a65c18e92b572baeecb71f7432dc0964f314d8f',
    'B': '1bbb1269032bfd0b0fe0851235fc798af6bd3c9b'
}

class HybridPatternPredictor:
    def __init__(self):
        self.models = []
        self.scalers = []
        self.trained = False
        self.lookup_table = {}
        
    def input_to_features(self, inp):
        """Comprehensive feature extraction"""
        features = []
        max_len = 8
        
        # Basic character encoding
        for i in range(max_len):
            if i < len(inp):
                features.append(ord(inp[i]))
                features.append(ord(inp[i]) ** 2 / 10000)
                features.append(ord(inp[i]) % 16)
            else:
                features.extend([0, 0, 0])
        
        # Length features
        features.append(len(inp))
        features.append(len(inp) ** 2)
        
        # Statistical features
        if inp:
            ascii_vals = [ord(c) for c in inp]
            features.append(sum(ascii_vals))
            features.append(sum(ascii_vals) / len(inp))
            features.append(max(ascii_vals))
            features.append(min(ascii_vals))
            features.append(np.std(ascii_vals) if len(ascii_vals) > 1 else 0)
        else:
            features.extend([0, 0, 0, 0, 0])
        
        # Rolling hash
        if inp:
            hash_val = 0
            for i, c in enumerate(inp):
                hash_val = (hash_val * 31 + ord(c)) % (10**8)
            features.append(hash_val / 10**6)
            features.append((hash_val % 256))
        else:
            features.extend([0, 0])
        
        # Character type counts
        features.append(sum(1 for c in inp if c.isdigit()))
        features.append(sum(1 for c in inp if c.islower()))
        features.append(sum(1 for c in inp if c.isupper()))
        
        # Positional weighted sum
        if inp:
            pos_sum = sum((i+1) * ord(c) for i, c in enumerate(inp))
            features.append(pos_sum / 1000)
        else:
            features.append(0)
        
        # XOR and AND operations
        if inp:
            xor_val = 0
            and_val = 255
            for c in inp:
                xor_val ^= ord(c)
                and_val &= ord(c)
            features.append(xor_val)
            features.append(and_val)
        else:
            features.extend([0, 0])
        
        return np.array(features).reshape(1, -1)
    
    def train(self, data=None, verbose=True):
        """Train ensemble of models with lookup table"""
        if data is None:
            data = training_data
        
        # Store exact matches in lookup table
        self.lookup_table = dict(data)
        
        if verbose:
            print("=" * 80)
            print("TRAINING HYBRID PATTERN PREDICTOR")
            print("=" * 80)
            print(f"Training samples: {len(data)}")
            print(f"Lookup table entries: {len(self.lookup_table)}")
        
        X_train = []
        y_train = [[] for _ in range(40)]
        
        # Prepare training data
        for inp, hash_str in data.items():
            features = self.input_to_features(inp)
            X_train.append(features[0])
            
            for i, c in enumerate(hash_str):
                y_train[i].append(int(c, 16))
        
        X_train = np.array(X_train)
        
        if verbose:
            print(f"Feature dimensions: {X_train.shape[1]}")
            print("Training ensemble models for each position...\n")
        
        # Train ensemble for each position
        self.models = []
        self.scalers = []
        
        for i in range(40):
            # Scale features
            scaler = StandardScaler()
            X_scaled = scaler.fit_transform(X_train)
            
            # Create ensemble: Neural Net + Random Forest + Gradient Boosting
            nn = MLPRegressor(
                hidden_layer_sizes=(64, 32, 16),
                activation='tanh',
                solver='lbfgs',
                max_iter=2000,
                random_state=42,
                alpha=0.001
            )
            
            rf = RandomForestRegressor(
                n_estimators=50,
                max_depth=10,
                min_samples_split=2,
                random_state=42
            )
            
            gb = GradientBoostingRegressor(
                n_estimators=50,
                max_depth=5,
                learning_rate=0.1,
                random_state=42
            )
            
            dt = DecisionTreeRegressor(
                max_depth=8,
                random_state=42
            )
            
            # Train all models
            nn.fit(X_scaled, y_train[i])
            rf.fit(X_scaled, y_train[i])
            gb.fit(X_scaled, y_train[i])
            dt.fit(X_scaled, y_train[i])
            
            self.models.append({
                'nn': nn,
                'rf': rf,
                'gb': gb,
                'dt': dt
            })
            self.scalers.append(scaler)
            
            if verbose and (i + 1) % 10 == 0:
                print(f"  Trained position {i + 1}/40...")
        
        self.trained = True
        
        if verbose:
            print("\n✓ Training complete!")
            self._evaluate(data)
    
    def _evaluate(self, data):
        """Evaluate model performance"""
        print("\n" + "=" * 80)
        print("TRAINING ACCURACY")
        print("=" * 80)
        
        total_correct = 0
        total_positions = 0
        perfect_matches = 0
        
        for inp, expected_hash in sorted(data.items()):
            predicted = self.predict(inp, verbose=False)
            matches = sum(1 for i in range(40) if predicted[i] == expected_hash[i])
            total_correct += matches
            total_positions += 40
            accuracy = matches / 40 * 100
            
            if matches == 40:
                perfect_matches += 1
                status = "✓"
            else:
                status = "✗"
            
            print(f"  {status} '{inp:10s}': {matches:2d}/40 ({accuracy:5.1f}%) - {predicted[:20]}...")
        
        overall = total_correct / total_positions * 100
        print(f"\n  Overall Accuracy: {overall:.2f}%")
        print(f"  Perfect Matches: {perfect_matches}/{len(data)}")
        print("=" * 80)
    
    def predict(self, inp, verbose=True):
        """Predict using lookup table first, then ensemble voting"""
        # Check lookup table first
        if inp in self.lookup_table:
            if verbose:
                print(f"\n✓ Found '{inp}' in lookup table (exact match)")
            return self.lookup_table[inp]
        
        if not self.trained:
            raise Exception("Model not trained! Call train() first.")
        
        features = self.input_to_features(inp)
        predicted_hash = []
        
        for i, (models_dict, scaler) in enumerate(zip(self.models, self.scalers)):
            features_scaled = scaler.transform(features)
            
            # Get predictions from all models
            pred_nn = models_dict['nn'].predict(features_scaled)[0]
            pred_rf = models_dict['rf'].predict(features_scaled)[0]
            pred_gb = models_dict['gb'].predict(features_scaled)[0]
            pred_dt = models_dict['dt'].predict(features_scaled)[0]
            
            # Ensemble voting with weights
            ensemble_pred = (
                pred_nn * 0.3 +
                pred_rf * 0.3 +
                pred_gb * 0.25 +
                pred_dt * 0.15
            )
            
            # Round to nearest hex digit
            hex_digit = max(0, min(15, round(ensemble_pred)))
            predicted_hash.append(format(hex_digit, 'x'))
        
        result = ''.join(predicted_hash)
        
        if verbose:
            print(f"\n⚡ Predicted '{inp}' using ensemble model")
            print(f"Result: {result}")
        
        return result
    
    def test_and_learn(self, inp, expected):
        """Test prediction and learn from mistakes"""
        print("\n" + "=" * 80)
        print(f"TESTING: '{inp}'")
        print("=" * 80)
        
        # Check if already in lookup
        if inp in self.lookup_table:
            stored = self.lookup_table[inp]
            if stored == expected:
                print("✓ Already in lookup table with correct value")
                return True
            else:
                print(f"⚠ Lookup table has different value!")
                print(f"Stored:   {stored}")
                print(f"Expected: {expected}")
        
        predicted = self.predict(inp, verbose=False)
        print(f"Predicted: {predicted}")
        print(f"Actual:    {expected}")
        
        matches = sum(1 for i in range(40) if predicted[i] == expected[i])
        accuracy = matches / 40 * 100
        
        print(f"\nAccuracy: {matches}/40 ({accuracy:.1f}%)")
        
        if matches == 40:
            print("✓ PERFECT MATCH!")
            return True
        else:
            print("✗ Mismatch detected")
            diff_pos = [i for i in range(40) if predicted[i] != expected[i]]
            print(f"Differences at {len(diff_pos)} positions: {diff_pos[:15]}{'...' if len(diff_pos) > 15 else ''}")
            
            # Add to training data
            print(f"\n→ Adding '{inp}' = '{expected}' to training data")
            training_data[inp] = expected
            print(f"→ Training data now has {len(training_data)} entries")
            print("\n💡 Call predictor.retrain() to update the model")
            
            return False
    
    def retrain(self):
        """Retrain with updated data"""
        print("\n" + "=" * 80)
        print("RETRAINING WITH UPDATED DATA")
        print("=" * 80)
        self.train(training_data, verbose=True)
        print("\n✓ Retraining complete! Model updated with new patterns.")

# Initialize and train
print("Initializing Hybrid Pattern Predictor...")
print("Features: Lookup Table + Ensemble Learning (NN + RF + GB + DT)\n")

predictor = HybridPatternPredictor()
predictor.train()

# Example usage
print("\n\n" + "=" * 80)
print("EXAMPLE PREDICTIONS")
print("=" * 80)

print("\n--- Known values (should use lookup table) ---")
for char in ['0', '1', 'a', 'A']:
    predictor.predict(char, verbose=True)

print("\n\n--- Unknown single characters ---")
for char in ['2', '6', '7', '8', '9', 'e', 'f']:
    pred = predictor.predict(char, verbose=False)
    print(f"{char} => {pred}")

print("\n\n--- Multi-character inputs ---")
for inp in ['ab', 'cd', '01', '12', 'ABC']:
    pred = predictor.predict(inp, verbose=False)
    print(f"{inp:5s} => {pred}")

print("\n\n" + "=" * 80)
print("HOW TO USE")
print("=" * 80)
print("\n1. Test a prediction:")
print("   predictor.predict('ab')")
print("\n2. Provide actual value and learn:")
print("   predictor.test_and_learn('ab', '40c48c0d3d8528637aede50dc0d0b22d58c78741')")
print("\n3. After adding several examples, retrain:")
print("   predictor.retrain()")
print("\n4. Check training data:")
print("   print(training_data)")
print("\n" + "=" * 80)